#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Check native import hyperlinks survive LibreOffice reopening the ODP."""
import argparse
import os
from pathlib import Path
import subprocess
import zipfile
from lxml import etree as E
from pptx import Presentation
from pptx.util import Inches

ROOT = Path(__file__).resolve().parents[4]
A = 'http://schemas.openxmlformats.org/drawingml/2006/main'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    deck = Presentation()
    slide = deck.slides.add_slide(deck.slide_layouts[6])
    paragraph = slide.shapes.add_textbox(Inches(1), Inches(1), Inches(7), Inches(2)).text_frame.paragraphs[0]
    paragraph.add_run().text = 'Before '
    link = paragraph.add_run()
    link.text = 'LINK_SENTINEL'
    link.font.bold = True
    link.hyperlink.address = 'https://example.org/pptx-test'
    paragraph.add_run().text = ' after'
    source = out/'source.pptx'
    deck.save(source)
    install = ROOT/'destdir/usr'
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_QPA_PLATFORMTHEME='generic',
               QT_LOGGING_RULES='*.debug=false', GSETTINGS_BACKEND='memory',
               LD_LIBRARY_PATH=str(ROOT/'build/bin')+':'+str(install/'lib/x86_64-linux-gnu'),
               QT_PLUGIN_PATH=str(install/'lib/x86_64-linux-gnu/qt6/plugins'),
               XDG_DATA_DIRS=str(install/'share')+':/usr/local/share:/usr/share')
    commands = [[ROOT/'build/bin/calligraconverter', '--batch', source, out/'native.odp'],
                ['/usr/bin/python3', Path(__file__).with_name('libreoffice_reference.py'), out/'native.odp', out/'reopened.pptx']]
    for index, command in enumerate(commands):
        with (out/f'conversion-{index}.log').open('wb') as log:
            subprocess.run(command, env=env, stdout=log, stderr=log, check=True, timeout=120)
    with zipfile.ZipFile(out/'reopened.pptx') as package:
        root = E.fromstring(package.read('ppt/slides/slide1.xml'))
        text = ''.join(node.text or '' for node in root.findall('.//{'+A+'}t'))
        assert text == 'Before LINK_SENTINEL after', 'Hyperlink text lost on ODP reopen'
        linked = root.xpath('//*[local-name()="r"][.//*[local-name()="hlinkClick"]]')
        assert linked and ''.join(linked[0].xpath('.//*[local-name()="t"]/text()')) == 'LINK_SENTINEL'
        assert linked[0].find('{'+A+'}rPr').get('b') == '1', 'Linked run formatting lost'
        rels = E.fromstring(package.read('ppt/slides/_rels/slide1.xml.rels'))
        assert any(rel.get('Target') == link.hyperlink.address for rel in rels), 'Hyperlink target lost'
    print('PASS: native ODP hyperlink text, target and bold formatting survive LibreOffice reopen')


if __name__ == '__main__':
    main()
