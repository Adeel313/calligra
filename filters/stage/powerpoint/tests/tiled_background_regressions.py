#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Check that tiled backgrounds remain visible through exported text boxes."""
import argparse
from io import BytesIO
import os
from pathlib import Path
import subprocess

import fitz
from PIL import Image, ImageDraw

import bitmap_background_regressions as fixture


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--install', type=Path, default=fixture.ROOT / 'build/install')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    install = args.install.resolve()
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_LOGGING_RULES='*.debug=false',
               GSETTINGS_BACKEND='memory',
               LD_LIBRARY_PATH=str(fixture.ROOT / 'build/bin') + ':' + str(install / 'lib/x86_64-linux-gnu'),
               QT_PLUGIN_PATH=str(install / 'lib/x86_64-linux-gnu/qt6/plugins'),
               XDG_DATA_DIRS=str(install / 'share') + ':/usr/local/share:/usr/share')
    image = Image.new('RGB', (32, 32), (20, 80, 160))
    draw = ImageDraw.Draw(image)
    draw.rectangle((0, 0, 15, 15), fill=(120, 190, 230))
    draw.rectangle((16, 16, 31, 31), fill=(120, 190, 230))
    data = BytesIO()
    image.save(data, format='PNG')
    fixture.PAGE_STYLE = fixture.PAGE_STYLE.replace('style:repeat="stretch"', 'style:repeat="repeat"')
    source, ppt, pdf = out / 'tiled.odp', out / 'tiled.ppt', out / 'tiled.pdf'
    fixture.build_background_odp(fixture.BASE, source, data.getvalue())
    commands = [
        [fixture.ROOT / 'build/bin/calligraconverter', '--batch', source, ppt],
        ['/usr/bin/python3', fixture.ROOT / 'filters/stage/pptx/tests/libreoffice_reference.py', ppt, pdf],
    ]
    for index, command in enumerate(commands):
        with (out / f'conversion-{index}.log').open('w') as log:
            subprocess.run(command, env=env, stdout=log, stderr=log, check=True, timeout=120)
    with fitz.open(pdf) as document:
        assert len(document) == 2
        assert 'ExportRoundTripTitle' in document[0].get_text()
        # This strip lies inside the title frame, below its text. A background
        # fill on the text box masks it white; a stretched bitmap has few edges.
        pixmap = document[0].get_pixmap(alpha=False)
        rendered = Image.frombytes('RGB', (pixmap.width, pixmap.height), pixmap.samples)
        pixels = list(rendered.crop((40, 110, 400, 120)).getdata())
        blue = sum(b > r + 50 and b > g for r, g, b in pixels)
        assert blue / len(pixels) > .95, 'Text box masks the tiled background'
        row = [rendered.getpixel((x, 115))[0] > 70 for x in range(40, 400)]
        edges = sum(a != b for a, b in zip(row, row[1:]))
        assert edges > 10, f'Background was stretched instead of tiled: {edges} edges'
    print('PASS: tiled background remains tiled and visible through text boxes')


if __name__ == '__main__':
    main()
