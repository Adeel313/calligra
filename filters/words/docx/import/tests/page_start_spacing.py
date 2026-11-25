#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Verify heading space before after explicit and automatic page breaks."""
import argparse
import os
from pathlib import Path
import subprocess

from docx import Document
from docx.shared import Pt
import pymupdf

ROOT = Path(__file__).resolve().parents[5]


def check(output):
    output.mkdir(parents=True, exist_ok=False)
    doc = Document()
    section = doc.sections[0]
    section.page_width = section.page_height = Pt(400)
    section.top_margin = section.bottom_margin = Pt(40)
    section.left_margin = section.right_margin = Pt(40)
    normal = doc.styles['Normal']
    normal.font.name = 'DejaVu Sans'
    normal.font.size = Pt(10)
    normal.paragraph_format.space_after = Pt(0)
    normal.paragraph_format.line_spacing = Pt(12)
    filler = doc.add_paragraph()
    filler.add_run('\n'.join(['Filler'] * 26))
    automatic = doc.add_paragraph('Automatic heading')
    automatic.paragraph_format.space_before = Pt(24)
    automatic.paragraph_format.keep_together = True
    explicit = doc.add_paragraph('Explicit heading')
    explicit.paragraph_format.space_before = Pt(24)
    explicit.paragraph_format.page_break_before = True
    source = output / 'page-start-spacing.docx'
    pdf = output / 'page-start-spacing.pdf'
    doc.save(source)
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen',
               XDG_CONFIG_HOME=str(output / 'config'))
    with (output / 'export.log').open('w') as log:
        subprocess.run([str(ROOT / 'run-words.sh'), '--export-pdf',
                        '--export-filename', str(pdf), str(source)],
                       env=env, stdout=log, stderr=log, check=True, timeout=90)
    with pymupdf.open(pdf) as rendered:
        assert len(rendered) == 3, len(rendered)
        auto = rendered[1].search_for('Automatic heading')[0]
        manual = rendered[2].search_for('Explicit heading')[0]
        first = rendered[0].search_for('Filler')[0]
        assert abs(auto.y0 - first.y0) < 0.5, (auto, first)
        assert abs(manual.y0 - auto.y0 - 24) < 0.5, (manual, auto)
    print('PASS: automatic page breaks suppress space above; explicit breaks retain it')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    check(parser.parse_args().output.resolve())
