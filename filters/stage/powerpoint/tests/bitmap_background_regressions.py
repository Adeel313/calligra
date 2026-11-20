#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Regression for binary .ppt bitmap-background export fidelity.

A bitmap drawing-page fill has no direct equivalent in the template used by
the native binary PPT exporter.  This test exports an ODP whose page fill is a
bitmap, re-imports the result, and checks that the fill survives as a
full-slide picture and that the slide text is still present.
"""
import argparse
import os
from pathlib import Path
import struct
import subprocess
import xml.etree.ElementTree as ET
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
BASE = HERE / 'data' / 'export_roundtrip.odp'

DRAW = 'urn:oasis:names:tc:opendocument:xmlns:drawing:1.0'
SVG = 'urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0'
TEXT = 'urn:oasis:names:tc:opendocument:xmlns:text:1.0'

PAGE_STYLE = (
    '<style:style style:name="dpBitmap" style:family="drawing-page">'
    '<style:drawing-page-properties draw:background-size="full" '
    'draw:fill="bitmap" draw:fill-image-name="bitmapBackground" '
    'style:repeat="stretch"/>'
    '</style:style>'
)
FILL_IMAGE = '<draw:fill-image draw:name="bitmapBackground" xlink:href="Pictures/bg.png" xlink:type="simple"/>'


def png_chunk(kind, payload):
    return (struct.pack('>I', len(payload)) + kind + payload
            + struct.pack('>I', zlib.crc32(kind + payload) & 0xFFFFFFFF))


def make_png():
    width = height = 2
    raw = b''.join(b'\x00' + bytes((0, 80, 160)) * width for _ in range(height))
    return (b'\x89PNG\r\n\x1a\n'
            + png_chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
            + png_chunk(b'IDAT', zlib.compress(raw))
            + png_chunk(b'IEND', b''))


def environment():
    install = ROOT / 'build/install'
    return dict(os.environ,
                QT_QPA_PLATFORM='offscreen', QT_QPA_PLATFORMTHEME='generic',
                QT_LOGGING_RULES='*.debug=false', GSETTINGS_BACKEND='memory',
                LD_LIBRARY_PATH=str(ROOT / 'build/bin') + ':' + str(install / 'lib/x86_64-linux-gnu'),
                QT_PLUGIN_PATH=str(install / 'lib/x86_64-linux-gnu/qt6/plugins'),
                XDG_DATA_DIRS=str(install / 'share') + ':/usr/local/share:/usr/share')


def build_background_odp(source, target, image):
    with zipfile.ZipFile(source) as zin, zipfile.ZipFile(target, 'w') as zout:
        for entry in zin.infolist():
            data = zin.read(entry.filename)
            if entry.filename == 'content.xml':
                text = data.decode()
                text = text.replace('<office:automatic-styles/>',
                                    '<office:automatic-styles>' + PAGE_STYLE
                                    + '</office:automatic-styles>', 1)
                text = text.replace(
                    '<draw:page draw:master-page-name="M1" draw:name="Slide One">',
                    '<draw:page draw:master-page-name="M1" draw:name="Slide One" '
                    'draw:style-name="dpBitmap">', 1)
                data = text.encode()
            elif entry.filename == 'styles.xml':
                text = data.decode()
                text = text.replace('<office:document-styles ',
                                    '<office:document-styles '
                                    'xmlns:xlink="http://www.w3.org/1999/xlink" ', 1)
                text = text.replace('<office:automatic-styles>',
                                    '<office:styles>' + FILL_IMAGE + '</office:styles>'
                                    '<office:automatic-styles>', 1)
                data = text.encode()
            elif entry.filename == 'META-INF/manifest.xml':
                text = data.decode()
                text = text.replace('</manifest:manifest>',
                                    '<manifest:file-entry manifest:full-path="Pictures/bg.png" '
                                    'manifest:media-type="image/png"/></manifest:manifest>', 1)
                data = text.encode()
            zout.writestr(entry, data)
        zout.writestr('Pictures/bg.png', image)


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def mm(value):
    value = value or '0'
    if value.endswith('cm'):
        return float(value[:-2]) * 10
    if value.endswith('mm'):
        return float(value[:-2])
    return float(value)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    out = parser.parse_args().output.resolve()
    out.mkdir(parents=True, exist_ok=False)

    image = make_png()
    source = out / 'bitmap_background.odp'
    ppt = out / 'bitmap_background.ppt'
    reimported = out / 'bitmap_background_rt.odp'
    build_background_odp(BASE, source, image)

    with (out / 'export.log').open('w') as log:
        subprocess.run([ROOT / 'build/bin/calligraconverter', '--batch',
                        '--mimetype', 'application/vnd.ms-powerpoint', source, ppt],
                       env=environment(), stdout=log, stderr=log, check=True, timeout=120)
    with (out / 'import.log').open('w') as log:
        subprocess.run([ROOT / 'build/bin/ppttoodp', ppt, reimported],
                       env=environment(), stdout=log, stderr=log, check=True, timeout=120)

    with zipfile.ZipFile(reimported) as package:
        content = package.read('content.xml').decode()
        images = [name for name in package.namelist() if name.startswith('Pictures/')]

    check(len(images) >= 2, 'bitmap background image was lost in round-trip')
    root = ET.fromstring(content)
    full_page = False
    for frame in root.iter('{%s}frame' % DRAW):
        if frame.find('{%s}image' % DRAW) is None:
            continue
        x, y = mm(frame.get('{%s}x' % SVG)), mm(frame.get('{%s}y' % SVG))
        w, h = mm(frame.get('{%s}width' % SVG)), mm(frame.get('{%s}height' % SVG))
        if abs(x) < 1 and abs(y) < 1 and abs(w - 280) < 1 and abs(h - 157.5) < 1:
            full_page = True
            break
    check(full_page, 'bitmap background is not a full-slide picture after round-trip')
    for text in ('ExportRoundTripTitle', 'FirstBodyLine', 'SecondBodyLine', 'SecondSlideText'):
        check(text in content, 'slide text %r was lost in round-trip' % text)
    print('PASS: binary .ppt round-trip preserves bitmap slide backgrounds')


if __name__ == '__main__':
    main()
