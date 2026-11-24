#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Headers and page-number fields inherit the document's default font."""
import argparse
import os
from pathlib import Path
import subprocess

from docx import Document
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
import pymupdf

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('output', type=Path)
parser.add_argument('--renderer', type=Path, help='Optional renderer accepting input DOCX and output PDF')
args = parser.parse_args()
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=False)
document = Document()
defaults = document.styles.element.find(qn('w:docDefaults')).find(qn('w:rPrDefault')).find(qn('w:rPr'))
fonts = defaults.find(qn('w:rFonts'))
fonts.attrib.clear()
for attribute in ('ascii', 'hAnsi', 'eastAsia', 'cs'):
    fonts.set(qn('w:' + attribute), 'Arial')
document.sections[0].header.paragraphs[0].text = 'DEFAULT HEADER FONT'
footer = document.sections[0].footer.paragraphs[0]
for kind in ('begin', 'separate', 'end'):
    run = footer.add_run()
    field = OxmlElement('w:fldChar')
    field.set(qn('w:fldCharType'), kind)
    run._r.append(field)
    if kind == 'begin':
        instruction = OxmlElement('w:instrText')
        instruction.text = 'PAGE'
        footer.add_run()._r.append(instruction)
    elif kind == 'separate':
        footer.add_run('1')
document.add_paragraph('First page body')
document.add_page_break()
document.add_paragraph('Second page body')
source, pdf = output / 'font-defaults.docx', output / 'font-defaults.pdf'
document.save(source)
root = Path(__file__).resolve().parents[5]
command = ([str(args.renderer.resolve()), str(source), str(pdf)] if args.renderer else
           [str(root / 'run-words.sh'), '--export-pdf', '--export-filename', str(pdf), str(source)])
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', XDG_CONFIG_HOME=str(output / 'config'))
with (output / 'export.log').open('w') as log:
    subprocess.run(command, env=env, stdout=log, stderr=log, check=True, timeout=90)
with pymupdf.open(pdf) as rendered:
    assert len(rendered) == 2, len(rendered)
    for number, page in enumerate(rendered, 1):
        spans = [s for b in page.get_text('dict')['blocks'] for line in b.get('lines', []) for s in line['spans']]
        header = [s for s in spans if 'DEFAULT HEADER FONT' in s['text']]
        footer = [s for s in spans if s['text'].strip() == str(number)]
        assert header and footer, (number, spans)
        assert all(s['font'].startswith('Arial') for s in header + footer), (number, header, footer)
print('PASS: default font in headers and page-number fields on both pages')
