#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Regression for native WMF/EMF preservation in binary .ppt export.

EMF/WMF previews of embedded OLE objects carry selectable text.  The binary
PPT exporter used to rasterise vector shapes to PNG, dropping that text.  The
exporter now emits a native metafile blip, and this test verifies that the
original bytes survive an ODP -> PPT -> ODP round-trip.
"""
import argparse
import os
from pathlib import Path
import subprocess
import struct
import zipfile
import zlib

import fitz
import olefile

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
BASE = HERE / 'data' / 'export_roundtrip.odp'
METAFILE = HERE / 'data' / 'metafile_fixture.emf'

FRAME = (
    '<draw:frame draw:layer="layout" svg:x="1cm" svg:y="12cm" '
    'svg:width="15cm" svg:height="3cm">'
    '<draw:image xlink:href="Pictures/metafile_fixture.emf" xlink:type="simple"/>'
    '</draw:frame>'
)
MANIFEST_ENTRY = (
    '<manifest:file-entry manifest:full-path="Pictures/metafile_fixture.emf" '
    'manifest:media-type="image/x-emf"/>'
)


def environment():
    install = ROOT / 'build/install'
    return dict(os.environ,
                QT_QPA_PLATFORM='offscreen', QT_QPA_PLATFORMTHEME='generic',
                QT_LOGGING_RULES='*.debug=false', GSETTINGS_BACKEND='memory',
                LD_LIBRARY_PATH=str(ROOT / 'build/bin') + ':' + str(install / 'lib/x86_64-linux-gnu'),
                QT_PLUGIN_PATH=str(install / 'lib/x86_64-linux-gnu/qt6/plugins'),
                XDG_DATA_DIRS=str(install / 'share') + ':/usr/local/share:/usr/share')


def build_odp(source, target, metafile):
    with zipfile.ZipFile(source) as zin, zipfile.ZipFile(target, 'w') as zout:
        for entry in zin.infolist():
            data = zin.read(entry.filename)
            if entry.filename == 'content.xml':
                text = data.decode()
                if 'xmlns:xlink=' not in text.split('>', 1)[0]:
                    text = text.replace('<office:document-content ',
                                        '<office:document-content '
                                        'xmlns:xlink="http://www.w3.org/1999/xlink" ', 1)
                text = text.replace('</draw:page>', FRAME + '</draw:page>', 1)
                data = text.encode()
            elif entry.filename == 'META-INF/manifest.xml':
                text = data.decode().replace('</manifest:manifest>',
                                             MANIFEST_ENTRY + '</manifest:manifest>', 1)
                data = text.encode()
            zout.writestr(entry, data)
        zout.writestr('Pictures/metafile_fixture.emf', metafile)


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--install', type=Path)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    env = environment()
    env['XDG_CONFIG_HOME'] = str(out / 'config')
    if args.install:
        install = args.install.resolve()
        env['QT_PLUGIN_PATH'] = str(install / 'lib/x86_64-linux-gnu/qt6/plugins')
        env['XDG_DATA_DIRS'] = str(install / 'share') + ':/usr/local/share:/usr/share'

    metafile = METAFILE.read_bytes()
    source = out / 'metafile.odp'
    ppt = out / 'metafile.ppt'
    reimported = out / 'metafile_rt.odp'
    build_odp(BASE, source, metafile)

    with (out / 'export.log').open('w') as log:
        subprocess.run([ROOT / 'build/bin/calligraconverter', '--batch',
                        '--mimetype', 'application/vnd.ms-powerpoint', source, ppt],
                       env=env, stdout=log, stderr=log, check=True, timeout=120)
    with (out / 'import.log').open('w') as log:
        subprocess.run([ROOT / 'build/bin/ppttoodp', ppt, reimported],
                       env=env, stdout=log, stderr=log, check=True, timeout=120)

    with zipfile.ZipFile(reimported) as package:
        content = package.read('content.xml').decode()
        candidates = [name for name in package.namelist()
                      if name.startswith('Pictures/') and name.lower().endswith('.emf')]
        check(bool(candidates), 'native EMF image was lost in round-trip')
        check(any(package.read(name) == metafile for name in candidates),
              'EMF bytes changed during round-trip')
        check('xlink:href="Pictures/' in content and '.emf"' in content,
              'reimported ODP does not reference an EMF image')

    # Native byte preservation alone misses malformed headers that our own
    # importer tolerates but independent readers reject.
    with olefile.OleFileIO(ppt) as storage:
        pictures = storage.openstream('Pictures').read()
        document = storage.openstream('PowerPoint Document').read()

    def check_picture_types(start, end):
        position = start
        while position + 8 <= end:
            version, kind, length = struct.unpack_from('<HHI', document, position)
            check(position + 8 + length <= end, 'Truncated drawing record')
            if kind == 0xF007:
                offset, = struct.unpack_from('<I', document, position + 36)
                _, blip_kind, _ = struct.unpack_from('<HHI', pictures, offset)
                expected = {0xF01A: 2, 0xF01B: 3, 0xF01D: 5, 0xF01E: 6}[blip_kind]
                check(version >> 4 == expected and document[position + 8:position + 10] == bytes([expected, expected]),
                      'Picture-store type does not match its BLIP')
            elif version & 15 == 15:
                check_picture_types(position + 8, position + 8 + length)
            position += 8 + length

    check_picture_types(0, len(document))
    position = 0
    checked = False
    while position + 8 <= len(pictures):
        _, kind, length = struct.unpack_from('<HHI', pictures, position)
        if kind == 0xF01A:
            header = position + 24  # record header + UID
            size, = struct.unpack_from('<I', pictures, header)
            compressed_size, = struct.unpack_from('<I', pictures, header + 28)
            check(pictures[header + 32:header + 34] == b'\x00\xfe', 'Invalid metafile compression/filter flags')
            data = pictures[header + 34:position + 8 + length]
            check(compressed_size == len(data) and size == len(metafile), 'Invalid metafile sizes')
            check(zlib.decompress(data) == metafile, 'Metafile compression lost data')
            check(pictures[header + 4:header + 20] == metafile[8:24],
                  'Metafile clipping bounds are not in metafile coordinates')
            width, height = struct.unpack_from('<ii', pictures, header + 20)
            check(abs(width - 5400000) <= 20 and abs(height - 1080000) <= 20,
                  'Metafile dimensions are not in EMUs')
            checked = True
        position += 8 + length
    check(checked, 'EMF BLIP not found')

    pdf = out / 'libreoffice-reopened.pdf'
    with (out / 'reference.log').open('w') as log:
        subprocess.run(['/usr/bin/python3', ROOT / 'filters/stage/pptx/tests/libreoffice_reference.py', ppt, pdf],
                       env=env, stdout=log, stderr=log, check=True, timeout=120)
    with fitz.open(pdf) as document:
        check('I am a sample document' in document[0].get_text(), 'LibreOffice lost the metafile artwork')
    print('PASS: native EMF bytes, valid BLIP headers and LibreOffice-visible artwork survive saving')


if __name__ == '__main__':
    main()
