#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Check missing UI fonts, inherited heading indents and line-based spacing."""
import os
from pathlib import Path
import subprocess
import sys

from docx import Document
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Pt
import pymupdf

ROOT = Path(__file__).resolve().parents[5]
output = Path(sys.argv[1]).resolve()
output.mkdir(parents=True, exist_ok=False)
document = Document()
document.styles['Normal'].font.name = 'Calibri'
document.styles['Normal'].font.size = Pt(12)
for family, text in [('Segoe UI', 'Readable body text'),
                     ('Segoe UI Semibold', 'Readable title'),
                     ('Segoe UI Emoji', 'WPS Office label')]:
    document.add_paragraph().add_run(text).font.name = family
heading = document.styles['Heading 3']
heading.font.name = 'Calibri'
heading.paragraph_format.left_indent = Pt(60)
heading.paragraph_format.first_line_indent = Pt(9)
document.add_paragraph('Indented heading', style=heading)
paragraph = document.add_paragraph('Aligned body')
paragraph.paragraph_format.left_indent = Pt(69)
for index, lines in enumerate([0, 100]):
    paragraph = document.add_paragraph('Spacing title %d' % index)
    paragraph.paragraph_format.space_before = Pt(0)
    paragraph.paragraph_format.space_after = Pt(40)
    spacing = paragraph._p.get_or_add_pPr().find(qn('w:spacing'))
    spacing.set(qn('w:afterLines'), str(lines))
    following = document.add_paragraph('Spacing following %d' % index)
    following.paragraph_format.space_before = Pt(0)
    following.paragraph_format.space_after = Pt(0)
for index, lines in enumerate([0, 100]):
    preceding = document.add_paragraph('Before preceding %d' % index)
    preceding.paragraph_format.space_after = Pt(0)
    paragraph = document.add_paragraph('Before target %d' % index)
    paragraph.paragraph_format.space_before = Pt(40)
    spacing = paragraph._p.get_or_add_pPr().find(qn('w:spacing'))
    spacing.set(qn('w:beforeLines'), str(lines))
source, pdf = output / 'ui-fonts.docx', output / 'ui-fonts.pdf'
document.save(source)
env = dict(os.environ, QT_QPA_PLATFORM='offscreen',
           XDG_CONFIG_HOME=str(output / 'config'))
with (output / 'export.log').open('w') as log:
    subprocess.run([str(ROOT / 'run-words.sh'), '--export-pdf', '--export-filename',
                    str(pdf), str(source)], env=env, stdout=log, stderr=log,
                   check=True, timeout=90)
with pymupdf.open(pdf) as rendered:
    assert len(rendered) == 1, len(rendered)
    page = rendered[0]
    for text in ['Readable body text', 'Readable title', 'WPS Office label']:
        matches = page.search_for(text)
        assert len(matches) == 1 and matches[0].height < 25, (text, matches)
    title = page.search_for('Indented heading')[0]
    body = page.search_for('Aligned body')[0]
    assert abs(title.x0 - body.x0) < 0.5, (title, body)
    gaps = [page.search_for('Spacing following %d' % i)[0].y0
            - page.search_for('Spacing title %d' % i)[0].y0 for i in range(2)]
    assert abs(gaps[1] - gaps[0] - 12) < 0.5, gaps
    assert gaps[0] < 30, gaps  # afterLines=0 overrides the 40pt fallback too.
    gaps = [page.search_for('Before target %d' % i)[0].y0
            - page.search_for('Before preceding %d' % i)[0].y0 for i in range(2)]
    assert abs(gaps[1] - gaps[0] - 12) < 0.5, gaps
    assert gaps[0] < 30, gaps
print('PASS: UI fonts, inherited heading indents, line-based spacing and zero override')
