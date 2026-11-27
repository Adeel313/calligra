#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Render Stage before and after saving an equation alongside overflowing text."""
import argparse
from io import BytesIO
import json
import os
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET
from zipfile import ZipFile

import fitz
from PIL import Image

from formula_roundtrip_regressions import FRAME, MATH, rewrite_content
from export_roundtrip_regressions import FIXTURE, ROOT, DRAW, TEXT
from bitmap_background_regressions import build_background_odp


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--install', type=Path, default=ROOT / 'build/install')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    install = args.install.resolve()
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_QPA_PLATFORMTHEME='generic',
               QT_LOGGING_RULES='*.debug=false', GSETTINGS_BACKEND='memory',
               XDG_CONFIG_HOME=str(out / 'config'),
               LD_LIBRARY_PATH=str(ROOT / 'build/bin') + ':' + str(install / 'lib/x86_64-linux-gnu'),
               QT_PLUGIN_PATH=str(install / 'lib/x86_64-linux-gnu/qt6/plugins'),
               XDG_DATA_DIRS=str(install / 'share') + ':/usr/local/share:/usr/share',
               CALLIGRA_STAGE_TEST_PDF='1', CALLIGRA_STAGE_TEST_BINARY_PPT='1')

    def run(command, name):
        with (out / (name + '.log')).open('w') as log:
            subprocess.run(command, env=env, stdout=log, stderr=log, check=True, timeout=120)

    # A fixed-height anchor is intentional. PowerPoint allows its text to
    # continue below it; importing it must not hide the later paragraphs.
    def edit(data):
        data = data.replace(b'svg:height="3cm"', b'svg:height="0.4cm"', 1)
        data = data.replace(b'<text:p>ExportRoundTripTitle</text:p>',
                            b'<text:p>ExportRoundTripTitle</text:p>'
                            b'<text:p>SECOND_VISIBLE_PARAGRAPH</text:p>'
                            b'<text:p>LAST_VISIBLE_PARAGRAPH</text:p>')
        return data.replace(b'</draw:page>', FRAME + b'</draw:page>', 1)

    source, ppt = out / 'equation-overflow.odp', out / 'equation-overflow.ppt'
    background = out / 'background.odp'
    image = BytesIO()
    Image.new('RGB', (16, 16), (245, 235, 205)).save(image, format='PNG')
    build_background_odp(FIXTURE, background, image.getvalue())
    rewrite_content(background, source, edit)
    run([ROOT / 'build/bin/calligraconverter', '--batch', source, ppt], 'initial-export')
    for revision in ('first', 'reopened'):
        target = out / revision
        run([ROOT / 'build/bin/stage_pptx_roundtrip', ppt, target], revision)
        result = json.loads((target / 'stage-result.json').read_text())
        assert result['opened'] and result['ppt_saved'] and result['slides'] == 2
        with fitz.open(target / 'native-stage.pdf') as document:
            visible = document[0].get_text()
            for marker in ('ExportRoundTripTitle', 'SECOND_VISIBLE_PARAGRAPH', 'LAST_VISIBLE_PARAGRAPH'):
                assert marker in visible, f'{revision}: Stage clipped {marker}'
            # PDF text extraction also sees text covered by a later picture.
            # Check rendered pixels so a full-slide image above the text fails.
            pixmap = document[0].get_pixmap(clip=fitz.Rect(28, 55, 440, 125), alpha=False)
            samples = pixmap.samples
            dark = sum(max(samples[i:i + 3]) < 80 for i in range(0, len(samples), 3))
            assert dark > 200, f'{revision}: Background picture covers the text'
        with ZipFile(target / 'stage.odp') as archive:
            root = ET.fromstring(archive.read('content.xml'))
            assert len(root.findall(f'.//{{{DRAW}}}page')) == 2
            assert root.find(f'.//{{{MATH}}}mfrac') is not None, 'Equation became an uneditable picture'
            texts = ''.join(''.join(p.itertext()) for p in root.findall(f'.//{{{TEXT}}}p'))
            for marker in ('FirstBodyLine', 'SecondBodyLine', 'SecondSlideText'):
                assert marker in texts, f'{revision}: Lost unrelated slide content'
        ppt = target / 'stage.ppt'
    print('PASS: Stage retains visible text, image stacking and editable equations across binary PPT saves')


if __name__ == '__main__':
    main()
