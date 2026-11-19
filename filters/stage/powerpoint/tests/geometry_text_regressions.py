#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Regression for rotated text boxes and math-run placement in binary .ppt.

Checks three fidelity properties that used to be lost on an ODP -> PPT -> ODP
round-trip:

* rotated text boxes keep their ODF draw:transform (and therefore their
  unrotated width/height);
* no-wrap plus auto-grow stay together, so a narrow shape does not wrap a
  base glyph away from its subscript;
* style:text-position keeps its exact percentage instead of becoming a
  generic +/-30% subscript.
"""
import argparse
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET
import zipfile

from formatting_regressions import environment

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
BASE = HERE / 'data' / 'export_roundtrip.odp'
DRAW_NS = 'urn:oasis:names:tc:opendocument:xmlns:drawing:1.0'
SVG_NS = 'urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0'

AUTO_STYLES = (
    '<office:automatic-styles>'
    '<style:style style:name="Gauto" style:family="graphic">'
    '<style:graphic-properties draw:auto-grow-height="true" '
    'draw:auto-grow-width="true" fo:wrap-option="no-wrap"/>'
    '</style:style>'
    '<style:style style:name="Tsub" style:family="text">'
    '<style:text-properties style:text-position="-25% 58%" fo:font-size="18pt"/>'
    '</style:style>'
    '</office:automatic-styles>'
)

FIRST_FRAME_RE = (
    r'<draw:frame\s+draw:layer="layout"\s+svg:x="1cm"\s+svg:y="2cm"\s+'
    r'svg:width="15cm"\s+svg:height="3cm">\s*'
    r'<draw:text-box>\s*<text:p>ExportRoundTripTitle</text:p>\s*</draw:text-box>\s*'
    r'</draw:frame>'
)

MATH_FRAME = (
    '<draw:frame draw:layer="layout" draw:style-name="Gauto" '
    'svg:x="1cm" svg:y="2cm" svg:width="1cm" svg:height="3cm">'
    '<draw:text-box><text:p><text:span>β</text:span>'
    '<text:span text:style-name="Tsub">3</text:span></text:p></draw:text-box>'
    '</draw:frame>'
)

SECOND_FRAME_RE = (
    r'<draw:frame\s+draw:layer="layout"\s+svg:x="2cm"\s+svg:y="3cm"\s+'
    r'svg:width="15cm"\s+svg:height="3cm">\s*'
    r'<draw:text-box>\s*<text:p>SecondSlideText</text:p>\s*</draw:text-box>\s*'
    r'</draw:frame>'
)

ROTATED_FRAME = (
    '<draw:frame draw:layer="layout" '
    'draw:transform="translate(-1.5cm -1cm) rotate(-0.802851) translate(5cm 5cm)" '
    'svg:width="3cm" svg:height="2cm">'
    '<draw:text-box><text:p>RotatedTime</text:p></draw:text-box>'
    '</draw:frame>'
)


def build_geometry_odp(source, target):
    with zipfile.ZipFile(source) as zin, zipfile.ZipFile(target, 'w') as zout:
        for entry in zin.infolist():
            data = zin.read(entry.filename)
            if entry.filename == 'content.xml':
                text = data.decode()
                if 'xmlns:fo=' not in text:
                    text = text.replace(
                        'xmlns:xlink=',
                        'xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" '
                        'xmlns:xlink=', 1)
                text = text.replace('<office:automatic-styles/>', AUTO_STYLES, 1)
                text, first_count = re.subn(FIRST_FRAME_RE, lambda _: MATH_FRAME, text, count=1)
                text, second_count = re.subn(SECOND_FRAME_RE, lambda _: ROTATED_FRAME, text, count=1)
                if first_count != 1 or second_count != 1:
                    raise RuntimeError('fixture templates did not match the base ODP')
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

    source = out / 'geometry.odp'
    ppt = out / 'geometry.ppt'
    reimported = out / 'geometry_rt.odp'
    build_geometry_odp(BASE, source)

    with (out / 'export.log').open('w') as log:
        subprocess.run([ROOT / 'build/bin/calligraconverter', '--batch',
                        '--mimetype', 'application/vnd.ms-powerpoint', source, ppt],
                       env=environment(), stdout=log, stderr=log, check=True, timeout=120)
    with (out / 'import.log').open('w') as log:
        subprocess.run([ROOT / 'build/bin/ppttoodp', ppt, reimported],
                       env=environment(), stdout=log, stderr=log, check=True, timeout=120)

    with zipfile.ZipFile(reimported) as package:
        content = package.read('content.xml').decode()

    check('rotate(-0.802851)' in content,
          'rotated text box lost its ODF transform in the .ppt round-trip')
    root = ET.fromstring(content)
    rotated = [frame for frame in root.iter('{%s}frame' % DRAW_NS)
               if 'RotatedTime' in ''.join(frame.itertext())]
    check(len(rotated) == 1, 'rotated text box text was lost')
    frame = rotated[0]
    width = frame.get('{%s}width' % SVG_NS)
    height = frame.get('{%s}height' % SVG_NS)
    def mm(value):
        number, unit = value[:-2], value[-2:]
        return float(number) * (1 if unit == 'mm' else 10)
    check(abs(mm(width) - 30) < 0.1 and abs(mm(height) - 20) < 0.1,
          'rotated text box width/height were swapped on export')

    graphic_styles = [
        m.group(0) for m in re.finditer(
            r'<style:style\b[^>]*style:family="graphic".*?</style:style>', content, re.S)
    ]
    check(any('fo:wrap-option="no-wrap"' in style and 'draw:auto-grow-width="true"' in style
              for style in graphic_styles),
          'no-wrap and auto-grow-width were not preserved together')
    check(not any('fo:wrap-option="no-wrap"' in style and '#18a303' in style
                  for style in graphic_styles),
          'template fill/outline leaked onto an exported text box')

    check(re.search(r'style:text-position="-25%(?:\s|")', content),
          'subscript position was not preserved exactly (-25%)')
    check('β' in content and '>3<' in content, 'subscript run text was lost')

    print('PASS: binary .ppt round-trip preserves rotation, no-wrap and subscript position')


if __name__ == '__main__':
    main()
