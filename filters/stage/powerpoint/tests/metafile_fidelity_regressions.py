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
import zipfile

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
    out = parser.parse_args().output.resolve()
    out.mkdir(parents=True, exist_ok=False)

    metafile = METAFILE.read_bytes()
    source = out / 'metafile.odp'
    ppt = out / 'metafile.ppt'
    reimported = out / 'metafile_rt.odp'
    build_odp(BASE, source, metafile)

    with (out / 'export.log').open('w') as log:
        subprocess.run([ROOT / 'build/bin/calligraconverter', '--batch',
                        '--mimetype', 'application/vnd.ms-powerpoint', source, ppt],
                       env=environment(), stdout=log, stderr=log, check=True, timeout=120)
    with (out / 'import.log').open('w') as log:
        subprocess.run([ROOT / 'build/bin/ppttoodp', ppt, reimported],
                       env=environment(), stdout=log, stderr=log, check=True, timeout=120)

    with zipfile.ZipFile(reimported) as package:
        content = package.read('content.xml').decode()
        candidates = [name for name in package.namelist()
                      if name.startswith('Pictures/') and name.lower().endswith('.emf')]
        check(bool(candidates), 'native EMF image was lost in round-trip')
        check(any(package.read(name) == metafile for name in candidates),
              'EMF bytes changed during round-trip')
        check('xlink:href="Pictures/' in content and '.emf"' in content,
              'reimported ODP does not reference an EMF image')

    print('PASS: native EMF metafile survives ODP -> binary .ppt -> ODP round-trip')


if __name__ == '__main__':
    main()
