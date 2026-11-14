#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Check PPTX structure, DrawingML coordinates and gradient directions."""
import argparse
import os
from pathlib import Path
import subprocess
import zipfile
from lxml import etree as E
from pptx import Presentation
from pptx.enum.shapes import MSO_SHAPE
from pptx.util import Inches

ROOT = Path(__file__).resolve().parents[4]
A = 'http://schemas.openxmlformats.org/drawingml/2006/main'
P = 'http://schemas.openxmlformats.org/presentationml/2006/main'
NS = {'p': P}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    deck = Presentation()
    for angle in (0, 5400000, 10800000, 16200000):
        slide = deck.slides.add_slide(deck.slide_layouts[6])
        shape = slide.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(1), Inches(1), Inches(4), Inches(3))
        shape.text = f'GRADIENT_{angle}'
        shape.fill.gradient()
        shape._element.spPr.find('.//{' + A + '}lin').set('ang', str(angle))
    deck.slides[0].notes_slide.notes_text_frame.text = 'PACKAGE_NOTES'
    source = out / 'directions.pptx'
    deck.save(source)
    install = ROOT / 'build/install'
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_QPA_PLATFORMTHEME='generic',
               QT_LOGGING_RULES='*.debug=false', GSETTINGS_BACKEND='memory',
               LD_LIBRARY_PATH=str(ROOT/'build/bin')+':'+str(install/'lib/x86_64-linux-gnu'),
               QT_PLUGIN_PATH=str(install/'lib/x86_64-linux-gnu/qt6/plugins'),
               XDG_DATA_DIRS=str(install/'share')+':/usr/local/share:/usr/share')
    with (out/'conversion.log').open('wb') as log:
        subprocess.run([ROOT/'build/bin/stage_pptx_roundtrip', source, out/'roundtrip'],
                       env=env, stdout=log, stderr=log, check=True, timeout=120)
    errors = []
    with zipfile.ZipFile(out/'roundtrip/stage.pptx') as package:
        for part in package.namelist():
            if not part.endswith('.xml'):
                continue
            root = E.fromstring(package.read(part))
            for tree in root.findall('.//p:spTree', NS):
                if tree.find('p:nvGrpSpPr/p:nvPr', NS) is None:
                    errors.append(f'{part}: missing group nonvisual properties')
            if root.tag in {f'{{{P}}}sldMaster', f'{{{P}}}notesMaster', f'{{{P}}}sldLayout'}:
                common = root.find('p:cSld', NS)
                if common is None or [child.tag for child in common] != [f'{{{P}}}spTree']:
                    errors.append(f'{part}: invalid common-slide children')
                required = 'p:clrMapOvr' if root.tag == f'{{{P}}}sldLayout' else 'p:clrMap'
                if root.find(required, NS) is None:
                    errors.append(f'{part}: missing root-level {required}')
                for layout in root.findall('p:sldLayoutIdLst/p:sldLayoutId', NS):
                    if not 2147483648 <= int(layout.get('id')) <= 4294967295:
                        errors.append(f'{part}: invalid slide layout ID')
            for offset in root.findall('.//{' + A + '}chOff'):
                if set(offset.attrib) != {'x', 'y'}:
                    errors.append(f'{part}: invalid child offset {dict(offset.attrib)}')
        for index, expected in enumerate((0, 5400000, 10800000, 16200000), 1):
            root = E.fromstring(package.read(f'ppt/slides/slide{index}.xml'))
            angles = [int(node.get('ang')) for node in root.findall('.//{' + A + '}lin')]
            if angles != [expected]:
                errors.append(f'slide {index}: expected gradient {expected}, got {angles}')
    assert not errors, '\n'.join(errors)
    print('PASS: DrawingML child offsets and four gradient directions')


if __name__ == '__main__':
    main()
