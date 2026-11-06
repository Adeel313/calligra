#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Benchmark and compare an existing PPTX using native Stage and LibreOffice."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
TESTS = ROOT / 'filters/stage/pptx/tests'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def analyze_filter_outputs(out, env):
    """Untimed, independent fidelity checks for each timed conversion direction."""
    import fitz
    import zipfile
    from PIL import Image, ImageChops, ImageStat
    spec = importlib.util.spec_from_file_location('pptx_compatibility', TESTS/'compatibility.py')
    compat = importlib.util.module_from_spec(spec);spec.loader.exec_module(compat)
    reference = TESTS/'libreoffice_reference.py'
    root = out/'filter-fidelity'
    root.mkdir(exist_ok=True)
    result = {}
    name = Path(json.loads((out/'report.json').read_text())['source']).stem
    baseline_pptx = out/'compatibility'/name/'reference.pptx'
    baseline_pdf = out/'compatibility'/name/'reference.pdf'
    for direction in ['import','export']:
        directory = root/direction;directory.mkdir(exist_ok=True)
        source = (out/'timing/odp/native/0/output.odp' if direction=='import'
                  else out/'timing/pptx/native/0/output.pptx')
        pdf = directory/'native.pdf'
        office = directory/'native.pptx'
        command = ['/usr/bin/python3',str(reference),str(source),str(pdf)]
        if direction=='import':command.append(str(office))
        with (directory/'render.log').open('wb') as log:
            process = subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=300)
        if process.returncode:
            result[direction] = dict(error=f'LibreOffice reopen exit {process.returncode}')
            continue
        if direction=='export':
            baseline_pptx = out/'timing/pptx/libreoffice/0/output.pptx'
            baseline_pdf = directory/'libreoffice.pdf'
            with (directory/'reference.log').open('wb') as log:
                subprocess.run(['/usr/bin/python3',str(reference),str(baseline_pptx),str(baseline_pdf)],
                    env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
        a = compat.features(office if direction=='import' else source)
        b = compat.features(out/'source.pptx' if direction=='import' else baseline_pptx)
        representation_counts = {key:dict(native=a[key],reference=b[key])
                                 for key in ['pictures','groups','connectors']}
        checks = {key:a[key]==b[key] for key in ['slides','slide_size','tables','charts',
                    'hidden','media','slide_texts','notes','links','chart_values','table_cells']}
        checks['slide_texts'] = {i:compat.normalize(t) for i,t in a['slide_texts'].items()} == {i:compat.normalize(t) for i,t in b['slide_texts'].items()}
        checks['notes'] = compat.normalize(a['notes']) == compat.normalize(b['notes'])
        checks['links'] = set(a['links']) == set(b['links'])
        measurements=json.loads((out/'report.json').read_text())['measurements']
        row=measurements[0 if direction=='import' else 1]
        checks['conversion_success']=all('error' not in sample for sample in row['engines']['native']['samples'])
        expected = fitz.open(baseline_pdf);actual = fitz.open(pdf)
        visual = []
        for i in range(min(len(expected),len(actual))):
            ep=expected[i].get_pixmap(alpha=False);ap=actual[i].get_pixmap(alpha=False)
            e=Image.frombytes('RGB',(ep.width,ep.height),ep.samples)
            a_image=Image.frombytes('RGB',(ap.width,ap.height),ap.samples).resize(e.size)
            diff=ImageChops.difference(e,a_image)
            mask=e.convert('L').point(lambda value:255 if value<245 else 0)
            total=1-sum(ImageStat.Stat(diff).mean)/(3*255)
            foreground=1-sum(ImageStat.Stat(diff,mask).mean)/(3*255) if mask.getbbox() else None
            visual.append(dict(slide=i+1,pixel_similarity=total,foreground_similarity=foreground))
            diff.save(directory/f'diff-{i+1}.png');a_image.save(directory/f'native-{i+1}.png')
        checks['rendering'] = len(actual)==len(expected)==b['slides'] and all(page['pixel_similarity']>=.92 for page in visual)
        result[direction]=dict(checks=checks,passed=all(checks.values()),representation_counts=representation_counts,
            native_features=compat.features(office if direction=='import' else source),
            reference_features=b,rendered_pages=len(actual),reference_pages=len(expected),visual=visual)
    packages=[]
    for path in [out/'source.pptx',*out.glob('timing/*/*/*/output.*')]:
        if path.suffix not in ('.pptx','.odp'):continue
        try:
            errors=[]
            with zipfile.ZipFile(path) as package:
                bad=package.testzip()
                for part in package.namelist():
                    if part.endswith(('.xml','.rels')):
                        try:compat.ET.fromstring(package.read(part))
                        except compat.ET.XMLSyntaxError as error:errors.append(dict(part=part,error=str(error)))
            packages.append(dict(path=str(path.relative_to(out)),crc_pass=bad is None,xml_pass=not errors,xml_errors=errors))
        except zipfile.BadZipFile:packages.append(dict(path=str(path),crc_pass=False,xml_pass=False))
    result['package_integrity']=packages
    (root/'report.json').write_text(json.dumps(result,indent=2,ensure_ascii=False))
    lines=[f'# Per-filter fidelity on {name}','', '| Direction | Result | Failed checks |','|---|---|---|']
    for direction in ['import','export']:
        r=result[direction]
        lines.append(f"| {direction} | {'PASS' if r.get('passed') else 'FAIL'} | "+
                     (r.get('error') or '; '.join(k for k,v in r.get('checks',{}).items() if not v))+' |')
    lines += ['', 'Full feature counts, per-slide visual scores, ZIP integrity results and difference images are saved alongside this report.']
    (root/'REPORT.md').write_text('\n'.join(lines)+'\n')
    return result


def write_report(out, report):
    case = report['cases'][0]
    original = case.get('source_features', {})
    lines = ['# '+Path(report['source']).name+' evaluation', '', report['reference_version'], '',
             f"Slides: {original.get('slides','unknown')}; source: {report['bytes']} bytes; build: {report['build_type']}.",
             f"{report['repeats']} runs per conversion. Fresh-process wall times include startup and I/O.", '',
             '| Conversion | Calligra seconds | LibreOffice seconds | Calligra peak MiB | LO peak MiB |',
             '|---|---:|---:|---:|---:|']
    for row in report['measurements']:
        n=row['engines']['native'];r=row['engines']['libreoffice']
        def seconds(value):return f'{value:.3f}' if value is not None else 'FAIL'
        def memory(value):return f'{value/1024:.1f}' if value is not None else 'FAIL'
        lines.append(f"| {row['direction']} | {seconds(n['median_seconds'])} | {seconds(r['median_seconds'])} | {memory(n['median_max_rss_kib'])} | {memory(r['median_max_rss_kib'])} |")
    lines += ['', '## Fidelity', '',
        'Native PPTX round trip: '+('PASS' if case.get('passed') else 'FAIL; '+ '; '.join(case.get('issues',[]))),
        'Independent import/export checks use the same input for both applications:', '',
        '| Direction | Result | Failed checks |', '|---|---|---|']
    for direction in ['import','export']:
        r=report['per_filter_fidelity'][direction]
        lines.append(f"| {direction} | {'PASS' if r.get('passed') else 'FAIL'} | "+(r.get('error') or '; '.join(k for k,v in r.get('checks',{}).items() if not v))+' |')
    exported=report['per_filter_fidelity']['export']
    lines += ['', (f"Common-ODP native export retains {exported['native_features']['pictures']} pictures; LibreOffice retains {exported['reference_features']['pictures']}." if 'native_features' in exported else 'Common-ODP native export failed: '+exported.get('error','unknown failure')), '',
        f"Source unchanged (SHA-256): {report['source_unchanged']}.",
        f"Source coverage: {len(original.get('text',''))} slide-text characters, {len(original.get('notes',''))} note characters, {original.get('tables',0)} tables, {original.get('charts',0)} charts. Features absent from the deck are not exercised.",
        'All applicable raw checks, individual wall/CPU/memory samples, feature counts and per-slide rendering diagnostics are in report.json.', '',
        '## Error handling', '']
    lines += [f"- {r['name']}: {'PASS' if r['passed'] else 'FAIL'}" for r in report['negative']]
    lines += ['', '## Visual checks', '',
        f"Stage renders compared with LibreOffice: {len(case.get('native_render_similarity',[]))} slides.",
        'Per-filter PDF render comparisons, foreground/pixel similarity scores and difference PNGs are in filter-fidelity/.',
        'Picture/group/connector counts are retained as representation diagnostics: vector images can become editable groups and lines can become paths.',
        'The rendering gate requires every slide to score at least 0.92; scores may hide localized errors and do not prove perceptual fidelity.']
    (out/'REPORT.md').write_text('\n'.join(lines)+'\n')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('input', type=Path)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--repeat', type=int, default=3)
    p.add_argument('--timeout', type=int, default=300)
    args = p.parse_args()
    if args.repeat < 1: p.error('repeat must be positive')
    source = args.input.resolve()
    out = args.output.resolve()
    if out.exists(): p.error('choose a new output directory')
    out.mkdir(parents=True)
    original_hash = digest(source)
    shutil.copy2(source, out / 'source.pptx')
    source_copy = out / 'source.pptx'
    install = ROOT / 'destdir/usr'
    env = os.environ.copy()
    env.update(QT_QPA_PLATFORM='offscreen', QT_QPA_PLATFORMTHEME='generic',
               QT_LOGGING_RULES='*.debug=false', GSETTINGS_BACKEND='memory', SAL_USE_VCLPLUGIN='svp',
               LD_LIBRARY_PATH=str(ROOT/'build/bin')+':'+str(install/'lib/x86_64-linux-gnu'),
               QT_PLUGIN_PATH=str(install/'lib/x86_64-linux-gnu/qt6/plugins'),
               XDG_DATA_DIRS=str(install/'share')+':/usr/local/share:/usr/share',
               XDG_CONFIG_HOME=str(out/'config'))
    spec = importlib.util.spec_from_file_location('pptx_compatibility', TESTS/'compatibility.py')
    compat = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(compat)
    converter = ROOT/'build/bin/calligraconverter'
    reference = TESTS/'libreoffice_reference.py'
    harness = ROOT/'build/bin/stage_pptx_roundtrip'
    report = dict(source=str(source), source_sha256=original_hash, bytes=source.stat().st_size,
                  repeats=args.repeat, timing_scope='fresh process, includes startup and I/O; warm filesystem cache',
                  build_type='Debug', measurements=[], cases=[])
    report['reference_version'] = subprocess.run(['/usr/bin/libreoffice','--version'], env=env,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=20).stdout.decode(errors='replace').strip()
    report['reference_version'] = next((line for line in report['reference_version'].splitlines() if line.startswith('LibreOffice ')),report['reference_version'])
    plugins = install/'lib/x86_64-linux-gnu/qt6/plugins/calligra/formatfilters'
    report['plugin_sha256'] = {name:digest(plugins/name) for name in
        ['calligra_filter_pptx2odp.so','calligra_filter_odp2pptx.so']}
    cache = (ROOT/'build/CMakeCache.txt').read_text()
    report['build_type'] = next((x.split('=',1)[1] for x in cache.splitlines() if x.startswith('CMAKE_BUILD_TYPE:')), 'unknown')

    def save(): (out/'report.json').write_text(json.dumps(report,indent=2,ensure_ascii=False))

    def execute(command, directory):
        directory.mkdir(parents=True, exist_ok=True)
        metrics = directory/'resources.txt'
        start = time.perf_counter()
        try:
            with (directory/'conversion.log').open('wb') as log:
                proc = subprocess.run(['/usr/bin/time','-f','%U %S %M','-o',str(metrics),*map(str,command)],
                    env=env, stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout)
            result = dict(seconds=time.perf_counter()-start, exit=proc.returncode)
            values = metrics.read_text().splitlines()[-1].split()
            result.update(cpu_seconds=float(values[0])+float(values[1]), max_rss_kib=int(values[2]))
            if proc.returncode: result['error'] = f'exit {proc.returncode}; see {directory}/conversion.log'
            return result
        except subprocess.TimeoutExpired:
            return dict(error=f'timeout after {args.timeout}s')

    def conversion(engine, input_path, output_path):
        command = ([converter,'--batch',input_path,output_path] if engine=='native' else
                   ['/usr/bin/python3',reference,input_path,output_path])
        result = execute(command, output_path.parent)
        if 'error' not in result:
            if output_path.is_file(): result['output_bytes'] = output_path.stat().st_size
            else: result['error'] = 'no output file'
        return result

    seed = out/'setup/reference.odp'
    report['setup'] = conversion('libreoffice',source_copy,seed)
    save()
    if 'error' in report['setup']: return 2
    for direction, input_path, suffix in [('PPTX import',source_copy,'odp'), ('PPTX export (common ODP input)',seed,'pptx'),
            ('PPTX export (native-import ODP)',out/'timing/odp/native/0/output.odp','pptx')]:
        row = dict(direction=direction, engines={})
        for engine in ['native','libreoffice']:
            samples=[]
            for i in range(args.repeat):
                group = 'native-roundtrip' if direction=='PPTX export (native-import ODP)' else suffix
                target=out/'timing'/group/engine/str(i)/('output.'+suffix)
                result=conversion(engine,input_path,target);samples.append(result)
                print(direction,engine,i+1,result,flush=True)
            valid=all('error' not in s for s in samples)
            row['engines'][engine]=dict(samples=samples,
                median_seconds=statistics.median(s['seconds'] for s in samples) if valid else None,
                median_max_rss_kib=statistics.median(s['max_rss_kib'] for s in samples) if valid else None)
        report['measurements'].append(row);save()
    # Exercise every existing structural, text, notes, table, chart and render check.
    original = compat.features(source_copy)
    case=dict(name=source.stem,path=str(source_copy),kind='core',
              tokens=list(original['slide_texts'].values()),notes=[original['notes']] if original['notes'] else [])
    report['cases'].append(compat.check_case(case,out/'compatibility',harness,env));save()
    case_dir=out/'compatibility'/source.stem
    # Compare both the native application's rendering and exported PPTX's LO rendering.
    import fitz
    from PIL import Image, ImageChops, ImageStat
    if (case_dir/'reopened.pdf').exists():
        baseline=fitz.open(case_dir/'reference.pdf'); exported=fitz.open(case_dir/'reopened.pdf')
        visual=[]
        for i in range(min(len(baseline),len(exported))):
            expected=baseline[i].get_pixmap(matrix=fitz.Matrix(1,1),alpha=False)
            actual=exported[i].get_pixmap(matrix=fitz.Matrix(1,1),alpha=False)
            a=Image.frombytes('RGB',(expected.width,expected.height),expected.samples)
            b=Image.frombytes('RGB',(actual.width,actual.height),actual.samples).resize(a.size)
            diff=ImageChops.difference(a,b);diff.save(case_dir/f'export-diff-{i+1}.png')
            b.save(case_dir/f'export-reference-{i+1}.png')
            visual.append(dict(slide=i+1,mean_pixel_similarity=1-sum(ImageStat.Stat(diff).mean)/(3*255)))
        report['export_visual_comparison']=visual
    report['per_filter_fidelity'] = analyze_filter_outputs(out, env)
    save()
    # Editing is separate from timing, since the harness also renders all slides.
    edited=out/'edit'
    report['edit_test_applicable'] = bool(original['text'])
    report['edit_process']=execute([harness,source_copy,edited,'--edit'],edited)
    if (edited/'stage.pptx').exists():
        report['edit_retained']='STAGE_EDIT_SENTINEL' in compat.features(edited/'stage.pptx')['text']
        report['edit_reopen']=execute(['/usr/bin/python3',reference,edited/'stage.pptx',edited/'reopened.pdf'],edited/'reopen')
    # Existing negative checks need an ODP seed for their backend-independent export test.
    (out/'fixtures').mkdir();shutil.copy2(seed,out/'fixtures/animation-source.odp')
    report['negative']=compat.negatives(out,ROOT/'build/bin/calligraofficeconverter',env,source_copy,harness)
    report['source_unchanged']=digest(source)==original_hash
    save()
    write_report(out, report)
    print(out/'REPORT.md',flush=True)
    return 0 if all(r.get('passed') for r in report['cases']) and all(report['per_filter_fidelity'][d].get('passed') for d in ['import','export']) and all(r['passed'] for r in report['negative']) and all(r['crc_pass'] and r['xml_pass'] for r in report['per_filter_fidelity']['package_integrity']) else 1


if __name__=='__main__': raise SystemExit(main())
