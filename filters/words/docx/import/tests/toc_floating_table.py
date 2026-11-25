#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Render cached TOC entries and text wrapped around a floating DOCX table."""
import argparse
import os
from pathlib import Path
import subprocess

from docx import Document
from docx.enum.style import WD_STYLE_TYPE
from docx.enum.text import WD_TAB_ALIGNMENT, WD_TAB_LEADER
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Inches, Pt
import pymupdf

ROOT = Path(__file__).resolve().parents[5]


def field(paragraph, kind, text=None):
    run = paragraph.add_run()
    element = OxmlElement('w:instrText' if text else 'w:fldChar')
    if text: element.text = text
    else: element.set(qn('w:fldCharType'), kind)
    run._r.append(element)


def check(output):
    output.mkdir(parents=True, exist_ok=False)
    doc = Document()
    doc.styles['Normal'].font.name = 'DejaVu Sans'
    doc.styles['Normal'].font.size = Pt(12)
    section = doc.sections[0]
    section.page_width = Inches(8.5)
    section.page_height = Inches(11)
    section.left_margin = section.right_margin = Inches(1)
    table = doc.add_table(rows=2, cols=2)
    table.autofit = False
    for column in table.columns: column.width = Inches(1.2)
    for row in table.rows:
        for cell in row.cells: cell.width = Inches(1.2)
    table.cell(0, 0).text = 'ITEM'
    table.cell(0, 1).text = 'NEEDED'
    table.cell(1, 0).text = 'Books'
    table.cell(1, 1).text = 'One'
    floating = OxmlElement('w:tblpPr')
    for key, value in [('vertAnchor', 'text'), ('tblpY', '1'), ('rightFromText', '187')]:
        floating.set(qn('w:' + key), value)
    table._tbl.tblPr.append(floating)
    doc.add_paragraph('Wrapped beside the table. ' * 20)
    drop = doc.add_paragraph('D')
    frame = OxmlElement('w:framePr')
    frame.set(qn('w:dropCap'), 'drop')
    frame.set(qn('w:lines'), '3')
    drop._p.get_or_add_pPr().append(frame)
    doc.add_paragraph('rop cap text continues beside the large initial. ' * 8)
    doc.add_page_break()
    toc = doc.styles.add_style('TOC2', WD_STYLE_TYPE.PARAGRAPH)
    toc.font.italic = True
    toc.font.name = 'DejaVu Sans'
    toc.paragraph_format.tab_stops.add_tab_stop(Inches(8), WD_TAB_ALIGNMENT.RIGHT, WD_TAB_LEADER.DOTS)
    link = doc.styles.add_style('Hyperlink', WD_STYLE_TYPE.CHARACTER)
    link.font.underline = True
    from docx.shared import RGBColor
    link.font.color.rgb = RGBColor(0, 0, 255)
    first = doc.add_paragraph(style=toc)
    field(first, 'begin')
    field(first, None, ' TOC \\o "1-2" \\h ')
    field(first, 'separate')
    for index, title in enumerate(['Entry alpha', 'Entry beta', 'Entry gamma']):
        p = first if index == 0 else doc.add_paragraph(style=toc)
        run = p.add_run(title)
        run.style = link
        run.font.size = Pt(12)
        p.add_run('\t8').font.size = Pt(12)
    field(doc.add_paragraph(), 'end')
    percent = doc.add_table(rows=1, cols=2)
    percent.autofit = False
    width = percent._tbl.tblPr.find(qn('w:tblW'))
    width.set(qn('w:type'), 'pct')
    width.set(qn('w:w'), '2500')  # 50% of the text area, not the stored grid
    percent.cell(0, 0).text = 'Percent left'
    percent.cell(0, 1).text = 'Percent right'
    source, pdf = output / 'toc-floating.docx', output / 'toc-floating.pdf'
    doc.save(source)
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', XDG_CONFIG_HOME=str(output / 'config'))
    with (output / 'export.log').open('w') as log:
        subprocess.run([ROOT / 'run-words.sh', '--export-pdf', '--export-filename', pdf, source],
                       env=env, stdout=log, stderr=log, check=True, timeout=90)
    with pymupdf.open(pdf) as rendered:
        assert len(rendered) == 2, len(rendered)
        header = rendered[0].search_for('NEEDED')[0]
        wrapped = rendered[0].search_for('Wrapped beside')[0]
        assert wrapped.x0 > header.x1 and wrapped.y0 < header.y1 + 20, (header, wrapped)
        spans = [s for b in rendered[0].get_text('dict')['blocks']
                 for line in b.get('lines', []) for s in line['spans'] if s['text'] == 'NEEDED']
        assert abs(spans[0]['size'] - 11) < 0.1, spans
        initials = [s for b in rendered[0].get_text('dict')['blocks']
                    for line in b.get('lines', []) for s in line['spans'] if s['text'] == 'D']
        assert initials and initials[0]['size'] > 30, initials
        entries = [s for b in rendered[1].get_text('dict')['blocks']
                   for line in b.get('lines', []) for s in line['spans'] if s['text'].startswith('Entry')]
        assert len(entries) == 3, entries
        numbers = rendered[1].search_for('8')
        for entry in entries:
            assert entry['flags'] & 2 and entry['color'] == 0, entry
            assert any(abs(n.y0 - entry['bbox'][1]) < 0.5 and n.x0 > 525 for n in numbers), (entry, numbers)
        left = rendered[1].search_for('Percent left')[0]
        right = rendered[1].search_for('Percent right')[0]
        assert abs(right.x0 - left.x0 - 117) < 1, (left, right)
    print('PASS: floating table wraps text; table defaults and TOC italic/tab formatting render correctly')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    check(parser.parse_args().output.resolve())
