#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Regression for unused PPT template text shapes leaking their style.

The native binary PPT template has two text placeholder shapes.  When an ODP
slide has fewer text boxes, the exporter empties the unused placeholders.
Emptying their text alone is not enough: the placeholders still carry the
template's green fill and dark-green outline, which reappear as large coloured
rectangles when the .ppt is imported back into Calligra.

This test exports a one-text-box slide, re-imports it and checks that no shape
in the result uses the template's green fill or outline.
"""
import argparse
import os
from pathlib import Path
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
BASE = HERE / 'data' / 'export_roundtrip.odp'


def environment():
    install = ROOT / 'build/install'
    return dict(os.environ,
                QT_QPA_PLATFORM='offscreen', QT_QPA_PLATFORMTHEME='generic',
                QT_LOGGING_RULES='*.debug=false', GSETTINGS_BACKEND='memory',
                LD_LIBRARY_PATH=str(ROOT / 'build/bin') + ':' + str(install / 'lib/x86_64-linux-gnu'),
                QT_PLUGIN_PATH=str(install / 'lib/x86_64-linux-gnu/qt6/plugins'),
                XDG_DATA_DIRS=str(install / 'share') + ':/usr/local/share:/usr/share')


def build_single_text_odp(source, target):
    with zipfile.ZipFile(source) as zin, zipfile.ZipFile(target, 'w') as zout:
        for entry in zin.infolist():
            data = zin.read(entry.filename)
            if entry.filename == 'content.xml':
                text = data.decode()
                # Remove every text frame, then add a single one.  The exporter's
                # template has two placeholders, so one will be left unused.
                import re
                text = re.sub(r'<draw:frame\b[^>]*>\s*<draw:text-box>.*?</draw:text-box>\s*</draw:frame>',
                              '', text, flags=re.S)
                frame = ('<draw:frame draw:layer="layout" svg:x="2cm" svg:y="2cm" '
                         'svg:width="15cm" svg:height="2cm">'
                         '<draw:text-box><text:p>OnlyText</text:p></draw:text-box></draw:frame>')
                text = text.replace('<draw:page draw:master-page-name="M1" draw:name="Slide One">',
                                    '<draw:page draw:master-page-name="M1" draw:name="Slide One">' + frame, 1)
                data = text.encode()
            zout.writestr(entry, data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    out = parser.parse_args().output.resolve()
    out.mkdir(parents=True, exist_ok=False)

    source = out / 'single_text.odp'
    ppt = out / 'single_text.ppt'
    reimported = out / 'single_text_rt.odp'
    build_single_text_odp(BASE, source)

    with (out / 'export.log').open('w') as log:
        subprocess.run([ROOT / 'build/bin/calligraconverter', '--batch',
                        '--mimetype', 'application/vnd.ms-powerpoint', source, ppt],
                       env=environment(), stdout=log, stderr=log, check=True, timeout=120)
    with (out / 'import.log').open('w') as log:
        subprocess.run([ROOT / 'build/bin/ppttoodp', ppt, reimported],
                       env=environment(), stdout=log, stderr=log, check=True, timeout=120)

    with zipfile.ZipFile(reimported) as package:
        content = package.read('content.xml').decode()
        styles = package.read('styles.xml').decode()
    assert 'OnlyText' in content, 'edited text lost'
    for leaked in ('#18a303', '#127a02'):
        assert leaked not in content, 'template shape leaked fill/outline into content: ' + leaked
        assert leaked not in styles, 'template shape leaked fill/outline into styles: ' + leaked
    print('PASS: unused template text shapes do not leak green fill or outline')


if __name__ == '__main__':
    main()
