#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Regression for legacy Microsoft Symbol font text in binary .ppt files.

PowerPoint stores Symbol-font glyphs as plain bytes in the text atom (a -> alpha,
m -> mu, 0xB4 -> multiplication sign).  Without the proprietary Symbol font
installed those bytes were rendered as Latin characters, so mathematical text
showed "abm" instead of "alpha beta mu".  The importer now decodes the symbol
encoding to Unicode and drops the symbol font family from the generated runs.

The fixture is produced with Calligra's own .ppt exporter from a small ODP with
a Symbol-font run, then imported again to exercise the decoding path.
"""
import argparse
from pathlib import Path
import re
import subprocess
import zipfile

from formatting_regressions import environment

ROOT = Path(__file__).resolve().parents[4]
BASE = Path(__file__).resolve().parent / 'data' / 'export_roundtrip.odp'

AUTO_STYLES = (
    '<office:automatic-styles>'
    '<style:style style:name="Tsym" style:family="text">'
    '<style:text-properties style:font-name="Symbol" fo:font-family="Symbol" '
    'fo:font-size="18pt"/>'
    '</style:style>'
    '</office:automatic-styles>'
)

# Legacy Symbol encoding for the characters below.
SOURCE_TEXT = 'abmeq´'
EXPECTED_TEXT = 'αβμεθ×'


def build_symbol_odp(source, target):
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
                text = text.replace(
                    '<text:p>ExportRoundTripTitle</text:p>',
                    '<text:p><text:span text:style-name="Tsym">' + SOURCE_TEXT +
                    '</text:span></text:p>', 1)
                data = text.encode()
            zout.writestr(entry, data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    out = parser.parse_args().output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    env = environment()

    source = out / 'symbol.odp'
    deck = out / 'symbol.ppt'
    target = out / 'imported.odp'
    build_symbol_odp(BASE, source)

    for src, dst in ((source, deck), (deck, target)):
        with (out / (dst.stem + '.log')).open('w') as log:
            subprocess.run([ROOT / 'build/bin/calligraconverter', '--batch', src, dst],
                           env=env, stdout=log, stderr=log, check=True, timeout=120)

    with zipfile.ZipFile(target) as package:
        content = package.read('content.xml').decode('utf-8')

    for char in EXPECTED_TEXT:
        assert char in content, 'Symbol-font byte was not decoded to Unicode: ' + repr(char)
    assert SOURCE_TEXT not in content, 'raw Symbol-font bytes were kept in the text'
    assert not re.search(r'<style:text-properties[^>]*fo:font-family="Symbol"', content), \
        'decoded runs still reference the Symbol font family'

    print('PASS: binary .ppt Symbol-font text imported as Unicode math symbols')


if __name__ == '__main__':
    main()
