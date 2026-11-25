#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""A first-page header's text and picture must not disappear or repeat."""
import io
import os
from pathlib import Path
import subprocess
import sys

from docx import Document
from docx.shared import Inches
import pymupdf
from PIL import Image

ROOT = Path(__file__).resolve().parents[5]
output = Path(sys.argv[1]).resolve()
output.mkdir(parents=True, exist_ok=False)
doc = Document()
section = doc.sections[0]
section.different_first_page_header_footer = True
section.header.paragraphs[0].text = 'FOLLOWING PAGE HEADER'
first = section.first_page_header.paragraphs[0]
first.text = 'FIRST PAGE HEADER'
picture = io.BytesIO()
Image.new('RGB', (120, 30), '#1368ce').save(picture, format='PNG')
picture.seek(0)
first.add_run().add_picture(picture, width=Inches(1.5))
section.first_page_footer.paragraphs[0].text = 'FIRST PAGE FOOTER'
section.footer.paragraphs[0].text = 'FOLLOWING PAGE FOOTER'
doc.add_paragraph('First page body')
doc.add_page_break()
doc.add_paragraph('Second page body')
source = output / 'first-header.docx'
doc.save(source)
pdf = output / 'first-header.pdf'
config = output / 'config'
config.mkdir()
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', XDG_CONFIG_HOME=str(config))
with (output / 'export.log').open('w') as log:
    subprocess.run([str(ROOT / 'run-words.sh'), '--export-pdf', '--export-filename',
                    str(pdf), str(source)], env=env, stdout=log, stderr=log,
                   check=True, timeout=90)
with pymupdf.open(pdf) as rendered:
    assert len(rendered) == 2, len(rendered)
    first, second = (page.get_text() for page in rendered)
    assert 'FIRST PAGE HEADER' in first, first
    assert 'FIRST PAGE FOOTER' in first, first
    assert 'FOLLOWING PAGE HEADER' not in first, first
    assert 'FOLLOWING PAGE HEADER' in second, second
    assert 'FOLLOWING PAGE FOOTER' in second, second
    assert 'FIRST PAGE HEADER' not in second, second
    assert len(rendered[0].get_images()) == 1
    assert not rendered[1].get_images()
print('PASS: first-page header text, artwork, footer, and continuation')
