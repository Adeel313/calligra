#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Regression for binary .ppt speaker-notes export fidelity.

Builds a small ODP with a <presentation:notes> element, exports it to a
binary .ppt with Calligra's native filter, re-imports it and checks that the
notes text survives as ODF paragraphs inside <presentation:notes>.

This guards buildNotesContainer() and the NotesList/persist-directory wiring
in PptExport.cpp against regressions.
"""
import argparse
import os
from pathlib import Path
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
BASE = HERE / 'data' / 'export_roundtrip.odp'

NOTES = (
    '<presentation:notes>'
    '<draw:frame presentation:class="notes" draw:layer="layout" '
    'svg:x="2cm" svg:y="10cm" svg:width="15cm" svg:height="8cm">'
    '<draw:text-box><text:p>First slide speaker note</text:p>'
    '<text:p>Second note line</text:p></draw:text-box>'
    '</draw:frame>'
    '<draw:page-thumbnail draw:layer="layout" svg:x="2cm" svg:y="2cm" '
    'svg:width="6cm" svg:height="4.5cm" draw:page-number="1"/>'
    '</presentation:notes>'
)


def environment():
    install = ROOT / 'build/install'
    return dict(os.environ,
                QT_QPA_PLATFORM='offscreen', QT_QPA_PLATFORMTHEME='generic',
                QT_LOGGING_RULES='*.debug=false', GSETTINGS_BACKEND='memory',
                LD_LIBRARY_PATH=str(ROOT / 'build/bin') + ':' + str(install / 'lib/x86_64-linux-gnu'),
                QT_PLUGIN_PATH=str(install / 'lib/x86_64-linux-gnu/qt6/plugins'),
                XDG_DATA_DIRS=str(install / 'share') + ':/usr/local/share:/usr/share')


def build_notes_odp(source, target):
    with zipfile.ZipFile(source) as zin, zipfile.ZipFile(target, 'w') as zout:
        for entry in zin.infolist():
            data = zin.read(entry.filename)
            if entry.filename == 'content.xml':
                text = data.decode()
                if 'xmlns:presentation=' not in text:
                    text = text.replace(
                        'xmlns:draw=',
                        'xmlns:presentation="urn:oasis:names:tc:opendocument:xmlns:presentation:1.0" '
                        'xmlns:draw=', 1)
                text = text.replace('</draw:page>', NOTES + '</draw:page>', 1)
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

    source = out / 'notes.odp'
    ppt = out / 'notes.ppt'
    reimported = out / 'notes_rt.odp'
    build_notes_odp(BASE, source)

    with (out / 'export.log').open('w') as log:
        subprocess.run([ROOT / 'build/bin/calligraconverter', '--batch',
                        '--mimetype', 'application/vnd.ms-powerpoint', source, ppt],
                       env=environment(), stdout=log, stderr=log, check=True, timeout=120)
    with (out / 'import.log').open('w') as log:
        subprocess.run([ROOT / 'build/bin/ppttoodp', ppt, reimported],
                       env=environment(), stdout=log, stderr=log, check=True, timeout=120)

    import_log = (out / 'import.log').read_text()
    for fatal in ('error parsing PowerPointStructs', 'bytes left at the end of PowerPointStructs'):
        check(fatal not in import_log, 'round-trip import failed: ' + fatal)

    with zipfile.ZipFile(reimported) as package:
        content = package.read('content.xml').decode()

    check('<presentation:notes' in content, 'speaker notes were lost in round-trip')
    notes = content[content.index('<presentation:notes'):]
    notes = notes[:notes.index('</presentation:notes>')]
    check('First slide speaker note' in notes, 'first notes paragraph was lost')
    check('Second note line' in notes, 'second notes paragraph was lost')
    print('PASS: binary .ppt round-trip preserves speaker notes')


if __name__ == '__main__':
    main()
