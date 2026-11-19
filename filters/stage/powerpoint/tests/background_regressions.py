#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Regression for binary .ppt solid-background export fidelity.

Builds a small ODP with a solid drawing-page fill, exports it to a binary
.ppt with Calligra's native filter, re-imports it and checks that the fill
survives as draw:fill / draw:fill-color on the drawing-page style.

This guards setSlideBackground() and the fMasterBackground clearing in
PptExport.cpp, which are required because the template slide inherits its
background from the master by default.
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

DRAW_PAGE_STYLE = (
    '<style:style style:name="dpBg" style:family="drawing-page">'
    '<style:drawing-page-properties draw:fill="solid" draw:fill-color="#336699"/>'
    '</style:style>'
)


def environment():
    install = ROOT / 'build/install'
    return dict(os.environ,
                QT_QPA_PLATFORM='offscreen', QT_QPA_PLATFORMTHEME='generic',
                QT_LOGGING_RULES='*.debug=false', GSETTINGS_BACKEND='memory',
                LD_LIBRARY_PATH=str(ROOT / 'build/bin') + ':' + str(install / 'lib/x86_64-linux-gnu'),
                QT_PLUGIN_PATH=str(install / 'lib/x86_64-linux-gnu/qt6/plugins'),
                XDG_DATA_DIRS=str(install / 'share') + ':/usr/local/share:/usr/share')


def build_background_odp(source, target):
    with zipfile.ZipFile(source) as zin, zipfile.ZipFile(target, 'w') as zout:
        for entry in zin.infolist():
            data = zin.read(entry.filename)
            if entry.filename == 'content.xml':
                text = data.decode()
                text = text.replace('<office:automatic-styles/>',
                                    '<office:automatic-styles>' + DRAW_PAGE_STYLE
                                    + '</office:automatic-styles>', 1)
                text = text.replace(
                    '<draw:page draw:master-page-name="M1" draw:name="Slide One">',
                    '<draw:page draw:master-page-name="M1" draw:name="Slide One" '
                    'draw:style-name="dpBg">', 1)
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

    source = out / 'background.odp'
    ppt = out / 'background.ppt'
    reimported = out / 'background_rt.odp'
    build_background_odp(BASE, source)

    with (out / 'export.log').open('w') as log:
        subprocess.run([ROOT / 'build/bin/calligraconverter', '--batch',
                        '--mimetype', 'application/vnd.ms-powerpoint', source, ppt],
                       env=environment(), stdout=log, stderr=log, check=True, timeout=120)
    with (out / 'import.log').open('w') as log:
        subprocess.run([ROOT / 'build/bin/ppttoodp', ppt, reimported],
                       env=environment(), stdout=log, stderr=log, check=True, timeout=120)

    with zipfile.ZipFile(reimported) as package:
        content = package.read('content.xml').decode()

    styles = ''
    for match in re.finditer(
            r'<style:style[^>]*style:family="drawing-page".*?</style:style>', content, re.S):
        if 'draw:fill-color' in match.group(0):
            styles = match.group(0)
            break
    check('draw:fill="solid"' in styles, 'solid background fill was lost in round-trip')
    check('draw:fill-color="#336699"' in styles, 'background colour was lost in round-trip')
    print('PASS: binary .ppt round-trip preserves solid slide backgrounds')


if __name__ == '__main__':
    main()
