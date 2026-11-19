#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Regression for importing binary .ppt files with msosptNil (0x0FFF) text shapes.

LibreOffice's "MS PowerPoint 97" export writes placeholder / text-box shapes with
OfficeArtFSP recInstance == 0x0FFF (msosptNil).  The generated MSO parser used to
reject recInstance values above 202, which aborted the whole PowerPoint Document
stream, and the shape dispatcher did not recognise msosptNil as a text box.  A
LibreOffice-authored .ppt therefore failed to import at all.

The fixture is a small LibreOffice-authored .ppt whose first two FSP records were
changed to recInstance == 0x0FFF, reproducing exactly that condition.
"""
import argparse
import os
from pathlib import Path
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
FIXTURE = HERE / 'data' / 'legacy_nilshape.ppt'
TEXT = 'urn:oasis:names:tc:opendocument:xmlns:text:1.0'


def environment():
    install = ROOT / 'build/install'
    return dict(os.environ,
                QT_QPA_PLATFORM='offscreen', QT_QPA_PLATFORMTHEME='generic',
                QT_LOGGING_RULES='*.debug=false', GSETTINGS_BACKEND='memory',
                LD_LIBRARY_PATH=str(ROOT / 'build/bin') + ':' + str(install / 'lib/x86_64-linux-gnu'),
                QT_PLUGIN_PATH=str(install / 'lib/x86_64-linux-gnu/qt6/plugins'),
                XDG_DATA_DIRS=str(install / 'share') + ':/usr/local/share:/usr/share')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    out = parser.parse_args().output.resolve()
    out.mkdir(parents=True, exist_ok=False)

    target = out / 'legacy.odp'
    with (out / 'conversion.log').open('w') as log:
        subprocess.run([ROOT / 'build/bin/calligraconverter', '--batch', FIXTURE, target],
                       env=environment(), stdout=log, stderr=log, check=True, timeout=120)

    with zipfile.ZipFile(target) as package:
        content = package.read('content.xml').decode('utf-8')

    for label in ('Discrete Mathematics Regression', 'Sets and logic', 'Margaret collected'):
        assert label in content, 'text shape with msosptNil was not imported: ' + label

    print('PASS: legacy .ppt with msosptNil text shapes imports with all text intact')


if __name__ == '__main__':
    main()
