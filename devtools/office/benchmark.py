#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Sequential end-to-end native filter/LibreOffice benchmark (isolated processes)."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import time
import zipfile
from lxml import etree

ROOT = Path(__file__).resolve().parents[2]
REFERENCE = ROOT / 'filters/stage/pptx/tests/libreoffice_reference.py'


def tokens(path):
    """Format-independent visible text smoke check; not a layout comparison."""
    with zipfile.ZipFile(path) as package:
        text = []
        for name in package.namelist():
            if name.endswith('.xml') and (name in ('content.xml', 'xl/sharedStrings.xml') or name.startswith(('word/', 'xl/worksheets/', 'ppt/slides/'))):
                tree = etree.fromstring(package.read(name))
                text.extend(tree.xpath('//text()'))
        return {word for word in ' '.join(text).split() if word.startswith('SENTINEL_')}


def fixtures(directory, size):
    from docx import Document
    from openpyxl import Workbook
    from pptx import Presentation
    directory.mkdir(parents=True, exist_ok=True)
    doc = Document()
    for i in range(size):
        doc.add_paragraph(f'SENTINEL_{i} café اردو benchmark paragraph')
    doc.save(directory / 'text.docx')
    book = Workbook()
    for i in range(size):
        book.active.append([f'SENTINEL_{i}', i, i * 2])
    book.save(directory / 'cells.xlsx')
    slides = Presentation()
    for i in range(max(1, size // 20)):
        slide = slides.slides.add_slide(slides.slide_layouts[1])
        slide.shapes.title.text = f'SENTINEL_{i}'
        slide.placeholders[1].text = 'café اردو benchmark slide'
    slides.save(directory / 'slides.pptx')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--build', type=Path, default=ROOT / 'build')
    parser.add_argument('--install', type=Path, default=ROOT / 'build/install')
    parser.add_argument('--repeat', type=int, default=3)
    parser.add_argument('--size', type=int, default=200)
    parser.add_argument('--timeout', type=float, default=120)
    args = parser.parse_args()
    if args.repeat < 1 or args.size < 1:
        parser.error('repeat and size must be positive')
    out = args.output.resolve()
    if (out / 'metadata.json').exists():
        parser.error('output directory already contains a benchmark; choose a fresh directory to avoid stale LibreOffice locks')
    out.mkdir(parents=True, exist_ok=True)
    fixtures(out / 'fixtures', args.size)
    install = args.install.resolve()
    env = os.environ.copy()
    env.update(GSETTINGS_BACKEND='memory', QT_QPA_PLATFORM='offscreen', QT_QPA_PLATFORMTHEME='generic',
               QT_LOGGING_RULES='*.debug=false',
               QT_PLUGIN_PATH=str(install / 'lib/x86_64-linux-gnu/qt6/plugins'),
               LD_LIBRARY_PATH=str(args.build.resolve() / 'bin') + ':' + str(install / 'lib/x86_64-linux-gnu'),
               XDG_DATA_DIRS=str(install / 'share') + ':/usr/local/share:/usr/share',
               XDG_CONFIG_HOME=str(out / 'config'))
    records = []
    version = subprocess.run(['/usr/bin/libreoffice', '--version'], env=env,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=20)
    version_text = version.stdout.decode(errors='replace').strip()
    version_text = next((line for line in version_text.splitlines() if line.startswith('LibreOffice ')), version_text)
    metadata = dict(reference_version=version_text,
                    repeat=args.repeat, size=args.size, timing='fresh-process wall clock',
                    build=str(args.build.resolve()), install=str(install))
    cache = args.build.resolve() / 'CMakeCache.txt'
    metadata['build_type'] = next((line.partition('=')[2] for line in cache.read_text().splitlines()
                                   if line.startswith('CMAKE_BUILD_TYPE:')), 'unknown') if cache.exists() else 'unknown'
    plugins = install / 'lib/x86_64-linux-gnu/qt6/plugins/calligra/formatfilters'
    metadata['plugin_sha256'] = {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
        for path in plugins.glob('calligra_filter_*.so')
        if any(name in path.name for name in ['docx2odt', 'odt2docx', 'xlsx2ods', 'ods2xlsx', 'pptx2odp', 'odp2pptx'])}
    (out / 'metadata.json').write_text(json.dumps(metadata, indent=2))

    def convert(engine, source, target):
        target.parent.mkdir(parents=True, exist_ok=True)
        target.unlink(missing_ok=True)
        command = ([str(args.build.resolve() / 'bin/calligraconverter'), '--batch', str(source), str(target)]
                   if engine == 'native' else ['/usr/bin/python3', str(REFERENCE), str(source), str(target)])
        start = time.perf_counter()
        try:
            result = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=args.timeout)
            elapsed = time.perf_counter() - start
            target.with_suffix(target.suffix + '.log').write_bytes(result.stdout)
            if result.returncode or not target.is_file():
                raise RuntimeError(f'exit {result.returncode}; see {target}.log')
            return dict(seconds=elapsed, bytes=target.stat().st_size, tokens=sorted(tokens(target)))
        except (subprocess.TimeoutExpired, RuntimeError, zipfile.BadZipFile, etree.XMLSyntaxError) as error:
            return dict(error=str(error))

    for stem, office, odf in [('text', 'docx', 'odt'), ('cells', 'xlsx', 'ods'), ('slides', 'pptx', 'odp')]:
        original = out / 'fixtures' / f'{stem}.{office}'
        seed = out / 'fixtures' / f'{stem}.{odf}'
        prepared = convert('libreoffice', original, seed)
        for source, extension in [(original, odf), (seed, office)]:
            row = dict(filter=f'{source.suffix[1:]}->{extension}', engines={})
            if 'error' in prepared:
                row['setup_error'] = prepared['error']
            else:
                for engine in ['native', 'libreoffice']:
                    samples = [convert(engine, source, out / row['filter'] / engine / str(i) / f'output.{extension}') for i in range(args.repeat)]
                    ok = [s['seconds'] for s in samples if 'seconds' in s]
                    row['engines'][engine] = dict(samples=samples, median_seconds=statistics.median(ok) if len(ok) == args.repeat else None)
                reference = row['engines']['libreoffice']['samples']
                native = row['engines']['native']['samples']
                row['text_smoke_pass'] = all('tokens' in n and 'tokens' in r and n['tokens'] == r['tokens'] == sorted(tokens(source)) for n, r in zip(native, reference))
                n = row['engines']['native']['median_seconds']
                r = row['engines']['libreoffice']['median_seconds']
                row['native_over_reference'] = n / r if n and r else None
            records.append(row)
            (out / 'report.json').write_text(json.dumps(records, indent=2))
            print(row['filter'], 'PASS' if row.get('text_smoke_pass') else 'FAIL', flush=True)
    lines = ['# Native Office filter benchmark', '', metadata['reference_version'], '',
             '| Filter | Native seconds | LibreOffice seconds | Native/reference | Text smoke |',
             '|---|---:|---:|---:|---|']
    for row in records:
        engines = row['engines']
        def number(value):
            return f'{value:.4f}' if value is not None else 'ERROR'
        lines.append('| ' + ' | '.join([row['filter'],
            number(engines.get('native', {}).get('median_seconds')),
            number(engines.get('libreoffice', {}).get('median_seconds')),
            number(row.get('native_over_reference')),
            'PASS' if row.get('text_smoke_pass') else 'FAIL']) + ' |')
    lines += ['', 'Fresh-process wall times include startup and I/O. Text smoke checks do not certify layout or full semantics.']
    (out / 'REPORT.md').write_text('\n'.join(lines) + '\n')
    return 0 if all(r.get('text_smoke_pass') for r in records) else 1


if __name__ == '__main__':
    raise SystemExit(main())
