#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Regression for native ODP -> binary .ppt -> ODP export/import round-trip.

The native PowerPoint export filter writes a binary .ppt from a template and
patches text, geometry and images into it.  This test feeds it a small ODP with
two slides, four distinct text strings and one embedded PNG, then imports the
resulting .ppt back to ODP and checks that slide count, text, images and
positions survive.  It also guards against the parser errors that stale
StyleTextPropAtom run counts and malformed shape records used to cause, and
against sample text from the template leaking onto exported slides.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import zipfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
FIXTURE = HERE / 'data' / 'export_roundtrip.odp'

DRAW = 'urn:oasis:names:tc:opendocument:xmlns:drawing:1.0'
SVG = 'urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0'
TEXT = 'urn:oasis:names:tc:opendocument:xmlns:text:1.0'
PRESENTATION = 'application/vnd.oasis.opendocument.presentation'

SOURCE_TEXTS = ['ExportRoundTripTitle', 'FirstBodyLine', 'SecondBodyLine', 'SecondSlideText']
TEMPLATE_TEXTS = ['Sets and logic', 'Margaret collected']


def environment():
    install = ROOT / 'build/install'
    return dict(os.environ,
                QT_QPA_PLATFORM='offscreen', QT_QPA_PLATFORMTHEME='generic',
                QT_LOGGING_RULES='*.debug=false', GSETTINGS_BACKEND='memory',
                LD_LIBRARY_PATH=str(ROOT / 'build/bin') + ':' + str(install / 'lib/x86_64-linux-gnu'),
                QT_PLUGIN_PATH=str(install / 'lib/x86_64-linux-gnu/qt6/plugins'),
                XDG_DATA_DIRS=str(install / 'share') + ':/usr/local/share:/usr/share')


def parse_content(odp):
    with zipfile.ZipFile(odp) as package:
        root = ET.fromstring(package.read('content.xml'))
    pages = root.findall('.//{%s}page' % DRAW)
    texts = [''.join(p.itertext()).strip() for p in root.findall('.//{%s}p' % TEXT)]
    images = root.findall('.//{%s}image' % DRAW)
    return pages, [t for t in texts if t], images


def frame_position(page, text):
    """Return (x, y, w, h) in mm for the text frame containing `text`."""
    for frame in page.findall('{%s}frame' % DRAW):
        if text in ''.join(frame.itertext()):
            def mm(attr):
                v = frame.get('{%s}%s' % (SVG, attr), '0')
                return float(v.replace('mm', '').replace('cm', '')) * (10 if 'cm' in v else 1)
            return (mm('x'), mm('y'), mm('width'), mm('height'))
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    out = parser.parse_args().output.resolve()
    out.mkdir(parents=True, exist_ok=False)

    ppt = out / 'roundtrip.ppt'
    odp = out / 'roundtrip.odp'
    with (out / 'export.log').open('w') as log:
        subprocess.run([ROOT / 'build/bin/calligraconverter', '--batch',
                        '--mimetype', 'application/vnd.ms-powerpoint', FIXTURE, ppt],
                       env=environment(), stdout=log, stderr=log, check=True, timeout=120)

    with (out / 'import.log').open('w') as log:
        subprocess.run([ROOT / 'build/bin/ppttoodp', ppt, odp],
                       env=environment(), stdout=log, stderr=log, check=True, timeout=120)

    import_log = (out / 'import.log').read_text()
    for fatal in ('error parsing PowerPointStructs', 'bytes left at the end of PowerPointStructs'):
        assert fatal not in import_log, 'round-trip import failed: ' + fatal

    src_pages, src_texts, src_images = parse_content(FIXTURE)
    pages, texts, images = parse_content(odp)

    assert len(pages) == len(src_pages) == 2, 'slide count changed: %d' % len(pages)
    for label in SOURCE_TEXTS:
        assert label in texts, 'text lost in round-trip: ' + label
    for label in TEMPLATE_TEXTS:
        assert label not in texts, 'template sample text leaked into export: ' + label
    assert len(images) == len(src_images) == 1, 'image lost in round-trip: %d' % len(images)

    # Source places the title at (1cm, 2cm) with size 15cm x 3cm.
    pos = frame_position(pages[0], 'ExportRoundTripTitle')
    assert pos is not None, 'title frame missing'
    expected = (10.0, 20.0, 150.0, 30.0)
    for got, want in zip(pos, expected):
        assert abs(got - want) < 0.5, 'position drift: got %s expected %s' % (pos, expected)

    print('PASS: ODP -> binary .ppt -> ODP preserves slides, text, image and positions')


if __name__ == '__main__':
    main()
