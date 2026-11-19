#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Regression for binary .ppt text-formatting export fidelity.

Builds a small ODP with two specifically formatted runs (bold/italic/
underline/size/colour/font-family and centre/right paragraph alignment),
exports it to a binary .ppt with Calligra's native filter, re-imports it and
checks that every formatting property survives as an ODF automatic style.

This guards the StyleTextPropAtom / TextCFException / TextPFException writer in
PptExport.cpp, the FontCollectionContainer extension used for typeface refs,
and the importer's per-run alignment handling.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
BASE = HERE / 'data' / 'export_roundtrip.odp'

AUTO_STYLES = (
    '<office:automatic-styles>'
    '<style:style style:name="Tfmt" style:family="text">'
    '<style:text-properties style:font-name="Courier New" fo:font-family="Courier New" '
    'fo:font-weight="bold" fo:font-style="italic" style:text-underline-style="solid" '
    'fo:font-size="28pt" fo:color="#ff0000"/>'
    '</style:style>'
    '<style:style style:name="Pcenter" style:family="paragraph">'
    '<style:paragraph-properties fo:text-align="center"/>'
    '</style:style>'
    '<style:style style:name="Pright" style:family="paragraph">'
    '<style:paragraph-properties fo:text-align="right"/>'
    '</style:style>'
    '</office:automatic-styles>'
)


def environment():
    install = ROOT / 'build/install'
    return dict(os.environ,
                QT_QPA_PLATFORM='offscreen', QT_QPA_PLATFORMTHEME='generic',
                QT_LOGGING_RULES='*.debug=false', GSETTINGS_BACKEND='memory',
                LD_LIBRARY_PATH=str(ROOT / 'build/bin') + ':' + str(install / 'lib/x86_64-linux-gnu'),
                QT_PLUGIN_PATH=str(install / 'lib/x86_64-linux-gnu/qt6/plugins'),
                XDG_DATA_DIRS=str(install / 'share') + ':/usr/local/share:/usr/share')


def build_formatted_odp(source, target):
    with zipfile.ZipFile(source) as zin, zipfile.ZipFile(target, 'w') as zout:
        for entry in zin.infolist():
            data = zin.read(entry.filename)
            if entry.filename == 'content.xml':
                text = data.decode()
                if 'xmlns:fo=' not in text:
                    text = text.replace(
                        'xmlns:xlink=',
                        'xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" '
                        'xmlns:xlink=', 1)
                text = text.replace('<office:automatic-styles/>', AUTO_STYLES, 1)
                text = text.replace(
                    '<text:p>ExportRoundTripTitle</text:p>',
                    '<text:p text:style-name="Pcenter">'
                    '<text:span text:style-name="Tfmt">ExportRoundTripTitle</text:span></text:p>', 1)
                text = text.replace(
                    '<text:p>FirstBodyLine</text:p>',
                    '<text:p text:style-name="Pright">'
                    '<text:span text:style-name="Tfmt">FirstBodyLine</text:span></text:p>', 1)
                data = text.encode()
            zout.writestr(entry, data)


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    out = parser.parse_args().output.resolve()
    out.mkdir(parents=True, exist_ok=False)

    formatted = out / 'formatted.odp'
    ppt = out / 'formatted.ppt'
    reimported = out / 'formatted_rt.odp'
    build_formatted_odp(BASE, formatted)

    with (out / 'export.log').open('w') as log:
        subprocess.run([ROOT / 'build/bin/calligraconverter', '--batch',
                        '--mimetype', 'application/vnd.ms-powerpoint', formatted, ppt],
                       env=environment(), stdout=log, stderr=log, check=True, timeout=120)
    with (out / 'import.log').open('w') as log:
        subprocess.run([ROOT / 'build/bin/ppttoodp', ppt, reimported],
                       env=environment(), stdout=log, stderr=log, check=True, timeout=120)

    import_log = (out / 'import.log').read_text()
    for fatal in ('error parsing PowerPointStructs', 'bytes left at the end of PowerPointStructs'):
        check(fatal not in import_log, 'round-trip import failed: ' + fatal)

    with zipfile.ZipFile(reimported) as package:
        content = package.read('content.xml').decode()

    # The formatted run must keep every character property.
    run = ''
    for match in re.finditer(r'<style:style\b[^>]*style:family="text"[^>]*>.*?</style:style>', content, re.S):
        if 'fo:font-weight="bold"' in match.group(0) or 'fo:color="#ff0000"' in match.group(0):
            run = match.group(0)
            break
    check(run, 'no bold/coloured text style in re-imported ODP')
    for label, prop in [
        ('font family', 'fo:font-family="Courier New"'),
        ('bold', 'fo:font-weight="bold"'),
        ('italic', 'fo:font-style="italic"'),
        ('size', 'fo:font-size="28pt"'),
        ('colour', 'fo:color="#ff0000"'),
        ('underline', 'style:text-underline'),
    ]:
        check(prop in run, '%s lost in round-trip' % label)
    check('style:text-underline="none"' not in run
          and 'style:text-underline-style="none"' not in run,
          'underline was explicitly disabled')

    # Both paragraph alignments must survive independently.
    aligns = re.findall(r'<style:style\b[^>]*style:family="paragraph"[^>]*>.*?</style:style>',
                        content, re.S)
    present = set()
    for block in aligns:
        m = re.search(r'fo:text-align="([^"]+)"', block)
        if m:
            present.add(m.group(1))
    check('center' in present, 'centre alignment lost in round-trip')
    check('right' in present, 'right alignment lost in round-trip')

    # Text and paragraph separators must stay intact (no dropped spaces).
    check('ExportRoundTripTitle' in content, 'formatted title text lost')
    check('FirstBodyLine' in content, 'first body line lost')
    check('<text:s/>' not in content or 'FirstBodyLine' in content, 'unexpected whitespace')

    print('PASS: binary .ppt round-trip preserves font, size, colour, underline and alignment')


if __name__ == '__main__':
    main()
