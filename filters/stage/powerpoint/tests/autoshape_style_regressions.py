#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Regression for text-bearing autoshapes in binary PPT export.

A draw:custom-shape (imported as a KoTosContainer) carries both editable text
and a visual fill/outline.  The exporter used to skip rasterising any shape
that yielded child text, so the shape's decoration was lost on a save/load
round-trip.  This test checks that the re-imported slide still contains both
the text and a picture covering the shape.
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


def build_autoshape_odp(source, target):
    shape = (
        '<draw:custom-shape draw:style-name="blueBox" draw:layer="layout" '
        'svg:x="2cm" svg:y="2cm" svg:width="10cm" svg:height="5cm">'
        '<text:p>BlueBoxText</text:p>'
        '<draw:enhanced-geometry svg:viewBox="0 0 21600 21600" '
        'draw:enhanced-path="M 0 0 L 21600 0 21600 21600 0 21600 0 0 Z N" '
        'draw:type="rectangle"/>'
        '</draw:custom-shape>'
    )
    style = (
        '<style:style style:name="blueBox" style:family="graphic">'
        '<style:graphic-properties draw:fill="solid" draw:fill-color="#2040c0" '
        'draw:stroke="none"/>'
        '</style:style>'
    )
    with zipfile.ZipFile(source) as zin, zipfile.ZipFile(target, 'w') as zout:
        for entry in zin.infolist():
            data = zin.read(entry.filename)
            if entry.filename == 'content.xml':
                text = data.decode()
                text = text.replace('<office:automatic-styles/>',
                                    '<office:automatic-styles>' + style + '</office:automatic-styles>', 1)
                text = text.replace('<draw:page draw:master-page-name="M1" draw:name="Slide One">',
                                    '<draw:page draw:master-page-name="M1" draw:name="Slide One">' + shape, 1)
                data = text.encode()
            zout.writestr(entry, data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    out = parser.parse_args().output.resolve()
    out.mkdir(parents=True, exist_ok=False)

    source = out / 'autoshape.odp'
    ppt = out / 'autoshape.ppt'
    reimported = out / 'autoshape_rt.odp'
    build_autoshape_odp(BASE, source)

    with (out / 'export.log').open('w') as log:
        subprocess.run([ROOT / 'build/bin/calligraconverter', '--batch',
                        '--mimetype', 'application/vnd.ms-powerpoint', source, ppt],
                       env=environment(), stdout=log, stderr=log, check=True, timeout=120)
    with (out / 'import.log').open('w') as log:
        subprocess.run([ROOT / 'build/bin/ppttoodp', ppt, reimported],
                       env=environment(), stdout=log, stderr=log, check=True, timeout=120)

    with zipfile.ZipFile(reimported) as package:
        content = package.read('content.xml').decode()
        images = [n for n in package.namelist() if n.startswith('Pictures/')]
    assert 'BlueBoxText' in content, 'autoshape text lost'
    assert len(images) >= 2, 'autoshape fill/outline was not rasterised (expected >1 image)'
    print('PASS: text-bearing autoshape keeps its fill/outline across round-trip')


if __name__ == '__main__':
    main()
