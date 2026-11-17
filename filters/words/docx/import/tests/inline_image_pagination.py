#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""An inline picture must not be enlarged by proportional paragraph spacing."""
import base64
import io
import os
from pathlib import Path
import subprocess
import sys
import zipfile
from lxml import etree as E
from docx import Document
from docx.shared import Pt

ROOT = Path(__file__).resolve().parents[5]
output = Path(sys.argv[1])
output.mkdir(parents=True, exist_ok=False)
doc = Document()
p = doc.add_paragraph()
p.paragraph_format.line_spacing = 1.5
p.paragraph_format.space_after = Pt(0)
p.add_run().add_picture(io.BytesIO(base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGP4//8/AAX+Av4N70a4AAAAAElFTkSuQmCC')), width=Pt(100), height=Pt(200))
doc.add_paragraph('AfterPicture')
run = doc.add_paragraph('AlternateFont').runs[0]
run.font.name = 'MissingPaginationTestFont'
source = output / 'input.docx'
doc.save(source)
W = 'http://schemas.openxmlformats.org/wordprocessingml/2006/main'
with zipfile.ZipFile(source) as z:
    entries = {n: z.read(n) for n in z.namelist()}
r = E.fromstring(entries['word/fontTable.xml'])
f = E.SubElement(r, '{'+W+'}font', {'{'+W+'}name': 'MissingPaginationTestFont'})
E.SubElement(f, '{'+W+'}altName', {'{'+W+'}val': 'Times New Roman'})
entries['word/fontTable.xml'] = E.tostring(r)
with zipfile.ZipFile(source, 'w') as z:
    for n, data in entries.items(): z.writestr(n, data)
install = ROOT / 'build/install'
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_LOGGING_RULES='*.debug=false',
           LD_LIBRARY_PATH=str(ROOT / 'build/bin'),
           QT_PLUGIN_PATH=str(install / 'lib/x86_64-linux-gnu/qt6/plugins'),
           XDG_DATA_DIRS=str(install / 'share') + ':/usr/local/share:/usr/share')
converter = str(ROOT / 'build/bin/calligraconverter')
odt = output / 'imported.odt'
subprocess.run([converter, '--batch', str(source), str(odt)], env=env, check=True, timeout=60)
with zipfile.ZipFile(odt) as z:
    styles = E.fromstring(z.read('styles.xml'))
ns = {'s': 'urn:oasis:names:tc:opendocument:xmlns:style:1.0', 'svg': 'urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0'}
face = styles.find('.//s:font-face[@s:name="MissingPaginationTestFont"]', ns)
assert face is not None and face.get('{'+ns['svg']+'}font-family').strip("'") == 'Times New Roman'
pdf = output / 'rendered.pdf'
subprocess.run([converter, '--batch', str(odt), str(pdf)], env=env, check=True, timeout=60)
boxes = output / 'boxes.html'
subprocess.run(['pdftotext', '-bbox', str(pdf), str(boxes)], check=True)
r = E.parse(str(boxes)); ns = {'h': 'http://www.w3.org/1999/xhtml'}
word = r.xpath('//h:word[text()="AfterPicture"]', namespaces=ns)[0]
# 72pt top margin + 200pt image + text metrics; no extra 100pt of 1.5 spacing.
y = float(word.get('yMin'))
assert 270 <= y < 310, y
print('PASS: inline image extent and missing-font alternate')
