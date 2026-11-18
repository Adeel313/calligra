#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Regress Wingdings/Symbol list markers rendering as unrelated Latin glyphs."""
import argparse
import subprocess
from pathlib import Path
from lxml import etree as E
from pptx import Presentation
from pptx.util import Inches, Pt
from export_options_regressions import environment

ROOT = Path(__file__).resolve().parents[4]
A = 'http://schemas.openxmlformats.org/drawingml/2006/main'
TEXT = 'urn:oasis:names:tc:opendocument:xmlns:text:1.0'
STYLE = 'urn:oasis:names:tc:opendocument:xmlns:style:1.0'
FO = 'urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0'


def add_bullet(paragraph, char, font):
    props = paragraph._p.get_or_add_pPr()
    props.set('marL', '342900')
    props.set('indent', '-342900')
    props.set('lvl', '0')
    if font:
        E.SubElement(props, '{'+A+'}buFont', typeface=font)
    E.SubElement(props, '{'+A+'}buChar', char=char)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    out = parser.parse_args().output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    source = out / 'bullets.pptx'
    deck = Presentation()
    slide = deck.slides.add_slide(deck.slide_layouts[6])
    frame = slide.shapes.add_textbox(Inches(1), Inches(1), Inches(8), Inches(5)).text_frame
    markers = [
        ('HOLLOW', '\u00a1', 'Wingdings'),   # Wingdings 0xA1: hollow circle
        ('FILLED', 'l', 'Wingdings'),        # Wingdings 0x6C: filled circle
        ('PUA_HOLLOW', '\uf0a1', 'Wingdings'),
        ('PUA_FILLED', '\uf06c', 'Wingdings'),
        ('UNICODE', '\u2022', None),
    ]
    for index, (label, char, font) in enumerate(markers):
        paragraph = frame.paragraphs[0] if index == 0 else frame.add_paragraph()
        paragraph.text = label
        paragraph.font.size = Pt(20)
        add_bullet(paragraph, char, font)
    deck.save(source)

    target = out / 'bullets.odp'
    with (out / 'conversion.log').open('w') as log:
        subprocess.run([ROOT/'build/bin/calligraconverter', '--batch', source, target],
                       env=environment(), stdout=log, stderr=log, check=True, timeout=120)

    import zipfile
    with zipfile.ZipFile(target) as package:
        roots = [E.fromstring(package.read(name)) for name in ['styles.xml', 'content.xml']]
    levels = [node for root in roots for node in root.iter('{'+TEXT+'}list-level-style-bullet')]
    chars = {node.get('{'+TEXT+'}bullet-char') for node in levels}
    assert {'○', '●', '•'} <= chars, chars
    assert not {'¡', 'l', '\uf0a1', '\uf06c'} & chars, chars
    fonts = {text_props.get('{'+FO+'}font-family') for level in levels
             for text_props in level.iter('{'+STYLE+'}text-properties')}
    assert 'Wingdings' not in fonts, fonts
    document_text = ''.join(text for root in roots for text in root.itertext())
    assert all(label in document_text
               for label in ['HOLLOW', 'FILLED', 'PUA_HOLLOW', 'PUA_FILLED', 'UNICODE'])
    print('PASS: Wingdings bullet markers imported as round Unicode glyphs')


if __name__ == '__main__':
    main()
