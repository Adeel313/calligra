#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Regress Symbol-font DOCX bullets vanishing when mapped to Unicode glyphs."""
import os
from pathlib import Path
import subprocess
import sys
import zipfile
from lxml import etree as E
from docx import Document

ROOT = Path(__file__).resolve().parents[5]
W = 'http://schemas.openxmlformats.org/wordprocessingml/2006/main'
TEXT = 'urn:oasis:names:tc:opendocument:xmlns:text:1.0'
STYLE = 'urn:oasis:names:tc:opendocument:xmlns:style:1.0'
FO = 'urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0'


def main():
    output = Path(sys.argv[1])
    output.mkdir(parents=True, exist_ok=False)
    source = output / 'input.docx'
    doc = Document()
    doc.add_paragraph('Bullets follow', style='List Bullet')
    doc.add_paragraph('Second item', style='List Bullet')
    doc.save(source)

    # Word writes the round marker as U+F0B7 in the Symbol font. Make sure the
    # fixture really exercises that path, not a plain Unicode bullet.
    with zipfile.ZipFile(source) as z:
        entries = {n: z.read(n) for n in z.namelist()}
    numbering = E.fromstring(entries['word/numbering.xml'])
    levels = [level for level in numbering.iter(f'{{{W}}}lvl')
              if (level.find(f'{{{W}}}numFmt') is not None
                  and level.find(f'{{{W}}}numFmt').get(f'{{{W}}}val') == 'bullet')]
    assert levels, 'fixture has no bullet numbering level'
    for level in levels:
        lvl_text = level.find(f'{{{W}}}lvlText')
        lvl_text.set(f'{{{W}}}val', '\uf0b7')
        rpr = level.find(f'{{{W}}}rPr')
        if rpr is None:
            rpr = E.SubElement(level, f'{{{W}}}rPr')
        fonts = rpr.find(f'{{{W}}}rFonts')
        if fonts is None:
            fonts = E.SubElement(rpr, f'{{{W}}}rFonts')
        for attr in ('ascii', 'hAnsi', 'cs'):
            fonts.set(f'{{{W}}}{attr}', 'Symbol')
    entries['word/numbering.xml'] = E.tostring(numbering)
    with zipfile.ZipFile(source, 'w') as z:
        for name, data in entries.items():
            z.writestr(name, data)

    install = ROOT / 'build/install'
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_LOGGING_RULES='*.debug=false',
               LD_LIBRARY_PATH=str(ROOT / 'build/bin'),
               QT_PLUGIN_PATH=str(install / 'lib/x86_64-linux-gnu/qt6/plugins'),
               XDG_DATA_DIRS=str(install / 'share') + ':/usr/local/share:/usr/share')
    target = output / 'imported.odt'
    subprocess.run([str(ROOT / 'build/bin/calligraconverter'), '--batch', str(source), str(target)],
                   env=env, check=True, timeout=60)

    with zipfile.ZipFile(target) as z:
        roots = [E.fromstring(z.read(name)) for name in ('content.xml', 'styles.xml')]
    style_by_name = {s.get(f'{{{STYLE}}}name'): s
                     for root in roots for s in root.iter(f'{{{STYLE}}}style')}
    levels = [node for root in roots for node in root.iter(f'{{{TEXT}}}list-level-style-bullet')]
    assert levels, 'no list-level-style-bullet was imported'
    chars = {node.get(f'{{{TEXT}}}bullet-char') for node in levels}
    assert '\u2022' in chars and '\uf0b7' not in chars, chars
    # The Unicode marker must not be left with the Symbol face, which has no
    # glyph at U+2022 and would render the bullet invisible. The font lives on
    # the text style referenced by text:style-name, not on the list level.
    fonts = set()
    for level in levels:
        style = style_by_name.get(level.get(f'{{{TEXT}}}style-name'))
        assert style is not None, 'bullet level has no text style'
        for props in style.iter(f'{{{STYLE}}}text-properties'):
            fonts.add(props.get(f'{{{FO}}}font-family') or props.get(f'{{{STYLE}}}font-name'))
    assert 'Symbol' not in fonts, fonts
    print('PASS: Symbol-font DOCX bullets imported as visible Unicode glyphs')


if __name__ == '__main__':
    main()
