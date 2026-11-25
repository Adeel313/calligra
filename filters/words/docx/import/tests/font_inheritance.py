#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Direct run formatting must keep paragraph and based-on character fonts."""
import os
from pathlib import Path
import subprocess
import sys

from docx import Document
from docx.enum.style import WD_STYLE_TYPE
from docx.shared import RGBColor
import pymupdf

ROOT = Path(__file__).resolve().parents[5]
output = Path(sys.argv[1]).resolve()
output.mkdir(parents=True, exist_ok=False)
document = Document()
document.styles['Normal'].font.name = 'Calibri'
paragraph = document.styles.add_style('ParagraphFont', WD_STYLE_TYPE.PARAGRAPH)
paragraph.base_style = document.styles['Normal']
paragraph.font.name = 'Times New Roman'
parent = document.styles.add_style('CharacterParent', WD_STYLE_TYPE.CHARACTER)
parent.font.name = 'Arial'
child = document.styles.add_style('CharacterChild', WD_STYLE_TYPE.CHARACTER)
child.base_style = parent
child.font.color.rgb = RGBColor(0x15, 0x68, 0xce)
run = document.add_paragraph(style=paragraph).add_run('ParagraphInherited')
run.font.color.rgb = RGBColor(0xce, 0x15, 0x15)
run = document.add_paragraph(style=paragraph).add_run('CharacterInherited')
run.style = child
source, pdf = output / 'fonts.docx', output / 'fonts.pdf'
document.save(source)
config = output / 'config'
config.mkdir()
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', XDG_CONFIG_HOME=str(config))
with (output / 'export.log').open('w') as log:
    subprocess.run([str(ROOT / 'run-words.sh'), '--export-pdf', '--export-filename',
                    str(pdf), str(source)], env=env, stdout=log, stderr=log,
                   check=True, timeout=90)
with pymupdf.open(pdf) as rendered:
    spans = [span for page in rendered for block in page.get_text('dict')['blocks']
             for line in block.get('lines', []) for span in line['spans']]
    for text, family in [('ParagraphInherited', 'TimesNewRoman'), ('CharacterInherited', 'Arial')]:
        matching = [span for span in spans if text in span['text']]
        assert matching and all(family in span['font'] for span in matching), (text, matching)
print('PASS: paragraph font and based-on character font inheritance')
