#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Regression for binary .ppt slide-transition export/import fidelity.

Builds a small ODP with a fade transition and 3-second auto advance, exports
to binary .ppt, re-imports and checks that the transition properties survive.
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
    '<style:style style:name="dpTrans" style:family="drawing-page">'
    '<style:drawing-page-properties presentation:transition-type="automatic" '
    'presentation:duration="PT3S" smil:type="fade" smil:subtype="crossfade" '
    'smil:dur="0.75"/>'
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


def build_transition_odp(source, target):
    with zipfile.ZipFile(source) as zin, zipfile.ZipFile(target, 'w') as zout:
        for entry in zin.infolist():
            data = zin.read(entry.filename)
            if entry.filename == 'content.xml':
                text = data.decode()
                if 'xmlns:smil=' not in text:
                    text = text.replace(
                        'xmlns:draw=',
                        'xmlns:smil="urn:oasis:names:tc:opendocument:xmlns:smil-compatible:1.0" '
                        'xmlns:presentation="urn:oasis:names:tc:opendocument:xmlns:presentation:1.0" '
                        'xmlns:draw=', 1)
                text = text.replace('<office:automatic-styles/>',
                                    '<office:automatic-styles>' + DRAW_PAGE_STYLE
                                    + '</office:automatic-styles>', 1)
                text = text.replace(
                    '<draw:page draw:master-page-name="M1" draw:name="Slide One">',
                    '<draw:page draw:master-page-name="M1" draw:name="Slide One" '
                    'draw:style-name="dpTrans">', 1)
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

    source = out / 'transition.odp'
    ppt = out / 'transition.ppt'
    reimported = out / 'transition_rt.odp'
    build_transition_odp(BASE, source)

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
        if 'smil:type' in match.group(0) or 'transition-type' in match.group(0):
            styles = match.group(0)
            break
    check('smil:type="fade"' in styles, 'transition type was lost in round-trip')
    check('smil:subtype="crossfade"' in styles, 'transition subtype was lost')
    check('smil:dur="0.75"' in styles, 'transition speed was lost')
    check('presentation:transition-type="automatic"' in styles, 'advance mode was lost')
    check('presentation:duration="PT3S"' in styles, 'advance duration was lost')
    print('PASS: binary .ppt round-trip preserves slide transitions')


if __name__ == '__main__':
    main()
