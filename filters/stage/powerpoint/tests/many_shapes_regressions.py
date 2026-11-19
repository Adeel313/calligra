#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Regression for exporting slides with more text shapes than the PPT template.

The native exporter clones its template's text shapes when an ODP slide has
more text boxes than the template provides.  The clones are appended to the
same QList that holds the original shapes, so storing raw Record* pointers
across those appends used to leave dangling pointers.  The result was
zero-length 0xF004 records, which made the whole .ppt unreadable by Calligra
(and by the parser on re-import).

This test exports a slide with eight text frames (the template has two), then
re-imports it and checks that every text survives and that no parse error is
reported.
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
TEXTS = ['TEXT_%d' % i for i in range(8)]


def environment():
    install = ROOT / 'build/install'
    return dict(os.environ,
                QT_QPA_PLATFORM='offscreen', QT_QPA_PLATFORMTHEME='generic',
                QT_LOGGING_RULES='*.debug=false', GSETTINGS_BACKEND='memory',
                LD_LIBRARY_PATH=str(ROOT / 'build/bin') + ':' + str(install / 'lib/x86_64-linux-gnu'),
                QT_PLUGIN_PATH=str(install / 'lib/x86_64-linux-gnu/qt6/plugins'),
                XDG_DATA_DIRS=str(install / 'share') + ':/usr/local/share:/usr/share')


def build_many_text_odp(source, target):
    frames = ''
    for i, text in enumerate(TEXTS):
        frames += (
            '<draw:frame draw:layer="layout" svg:x="1cm" svg:y="%dcm" '
            'svg:width="15cm" svg:height="1cm">'
            '<draw:text-box><text:p>%s</text:p></draw:text-box></draw:frame>\n'
            % (1 + i * 2, text))
    with zipfile.ZipFile(source) as zin, zipfile.ZipFile(target, 'w') as zout:
        for entry in zin.infolist():
            data = zin.read(entry.filename)
            if entry.filename == 'content.xml':
                text = data.decode()
                text = text.replace(
                    '<draw:page draw:master-page-name="M1" draw:name="Slide One">',
                    '<draw:page draw:master-page-name="M1" draw:name="Slide One">' + frames,
                    1)
                data = text.encode()
            zout.writestr(entry, data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    out = parser.parse_args().output.resolve()
    out.mkdir(parents=True, exist_ok=False)

    source = out / 'many_shapes.odp'
    ppt = out / 'many_shapes.ppt'
    reimported = out / 'many_shapes_rt.odp'
    build_many_text_odp(BASE, source)

    with (out / 'export.log').open('w') as log:
        subprocess.run([ROOT / 'build/bin/calligraconverter', '--batch',
                        '--mimetype', 'application/vnd.ms-powerpoint', source, ppt],
                       env=environment(), stdout=log, stderr=log, check=True, timeout=120)
    with (out / 'import.log').open('w') as log:
        subprocess.run([ROOT / 'build/bin/ppttoodp', ppt, reimported],
                       env=environment(), stdout=log, stderr=log, check=True, timeout=120)

    import_log = (out / 'import.log').read_text()
    for fatal in ('error parsing PowerPointStructs', 'bytes left at the end of PowerPointStructs'):
        assert fatal not in import_log, 'round-trip import failed: ' + fatal

    with zipfile.ZipFile(reimported) as package:
        content = package.read('content.xml').decode()
    for text in TEXTS:
        assert text in content, 'text lost after cloning text shapes: ' + text

    print('PASS: extra text shapes export without empty 0xF004 records and round-trip intact')


if __name__ == '__main__':
    main()
