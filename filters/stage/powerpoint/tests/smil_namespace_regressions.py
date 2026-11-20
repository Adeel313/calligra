#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Regression for ODP generated from PPT slides with transitions.

PptToOdp writes smil:type/subtype/dur properties into content.xml.  The
namespace must be declared on office:document-content, otherwise the ODP is
not well-formed XML and Calligra cannot load the file it just produced.
"""
import argparse
import os
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET
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
                    text = text.replace('xmlns:draw=',
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    out = parser.parse_args().output.resolve()
    out.mkdir(parents=True, exist_ok=False)

    source = out / 'transition.odp'
    ppt = out / 'transition.ppt'
    imported = out / 'imported.odp'
    pdf = out / 'check.pdf'
    build_transition_odp(BASE, source)

    with (out / 'export.log').open('w') as log:
        subprocess.run([ROOT / 'build/bin/calligraconverter', '--batch',
                        '--mimetype', 'application/vnd.ms-powerpoint', source, ppt],
                       env=environment(), stdout=log, stderr=log, check=True, timeout=120)
    with (out / 'import.log').open('w') as log:
        subprocess.run([ROOT / 'build/bin/ppttoodp', ppt, imported],
                       env=environment(), stdout=log, stderr=log, check=True, timeout=120)

    with zipfile.ZipFile(imported) as package:
        content = package.read('content.xml')
    assert b'smil:type' in content, 'transition was lost: no smil property generated'
    ET.fromstring(content)  # raises ParseError if xmlns:smil is missing
    assert b'xmlns:smil=' in content, 'smil prefix used without declaration'

    # The generated ODP must be loadable again.
    with (out / 'reload.log').open('w') as log:
        subprocess.run([ROOT / 'build/bin/calligraconverter', '--batch',
                        '--mimetype', 'application/pdf', imported, pdf],
                       env=environment(), stdout=log, stderr=log, check=True, timeout=120)
    print('PASS: PPT->ODP transition styles declare xmlns:smil and reload cleanly')


if __name__ == '__main__':
    main()
