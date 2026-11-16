#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Ensure DOCX equations inherit paragraph sizes and retain run overrides."""
import os
from pathlib import Path
import subprocess
import sys
import zipfile
from lxml import etree as E
from docx import Document
from docx.oxml import parse_xml
from docx.shared import Pt
from docx.enum.style import WD_STYLE_TYPE

ROOT = Path(__file__).resolve().parents[5]
output = Path(sys.argv[1])
output.mkdir(parents=True, exist_ok=False)
doc = Document()
doc.styles['Normal'].font.size = Pt(11)
base = doc.styles.add_style('MathBase', WD_STYLE_TYPE.PARAGRAPH)
base.font.size = Pt(13)
derived = doc.styles.add_style('MathDerived', WD_STYLE_TYPE.PARAGRAPH)
derived.base_style = base
W = 'http://schemas.openxmlformats.org/wordprocessingml/2006/main'
M = 'http://schemas.openxmlformats.org/officeDocument/2006/math'
for style, size, override in [('Normal', None, None), ('MathDerived', None, None), ('Normal', 9, 18)]:
    p = doc.add_paragraph(style=style)
    if size:
        p._p.get_or_add_pPr().append(parse_xml(f'<w:rPr xmlns:w="{W}"><w:sz w:val="{size*2}"/></w:rPr>'))
    props = f'<w:rPr><w:sz w:val="{override*2}"/></w:rPr>' if override else ''
    p._p.append(parse_xml(f'<m:oMath xmlns:m="{M}" xmlns:w="{W}"><m:sSub><m:e><m:r>{props}<m:t>x</m:t></m:r></m:e><m:sub><m:r><m:t>i</m:t></m:r></m:sub></m:sSub><m:r><m:t>,</m:t></m:r><m:r><m:t>1.5</m:t></m:r></m:oMath>'))
p = doc.add_paragraph()
p._p.append(parse_xml(f'<m:oMathPara xmlns:m="{M}" xmlns:w="{W}"><m:oMathParaPr><m:jc m:val="center"/></m:oMathParaPr><m:oMath><m:d><m:dPr><m:begChr m:val="["/><m:endChr m:val="]"/><m:sepChr m:val=""/></m:dPr><m:e><m:nary><m:naryPr><m:chr m:val="∑"/><m:subHide m:val="off"/><m:supHide m:val="off"/></m:naryPr><m:sub><m:r><m:t>i</m:t></m:r></m:sub><m:sup><m:r><m:t>T</m:t></m:r></m:sup><m:e><m:r><m:t>x</m:t></m:r></m:e></m:nary></m:e></m:d></m:oMath></m:oMathPara>'))
source = output / 'input.docx' 
target = output / 'imported.odt'
doc.save(source)
install = ROOT / 'build/install'
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_LOGGING_RULES='*.debug=false',
           LD_LIBRARY_PATH=str(ROOT / 'build/bin'),
           QT_PLUGIN_PATH=str(install / 'lib/x86_64-linux-gnu/qt6/plugins'),
           XDG_DATA_DIRS=str(install / 'share') + ':/usr/local/share:/usr/share')
subprocess.run([ROOT / 'build/bin/calligraconverter', '--batch', source, target], env=env, check=True, timeout=60)
with zipfile.ZipFile(target) as package:
    formulas = [E.fromstring(package.read(f'Object {i}/content.xml')) for i in range(1, 4)]
ns = {'m': 'http://www.w3.org/1998/Math/MathML'}
assert [f.find('m:mstyle', ns).get('fontsize') for f in formulas] == ['11pt', '13pt', '9pt']
assert formulas[2].find('.//m:mi', ns).get('fontsize') == '18pt'
assert all(f.find('.//m:msub', ns) is not None for f in formulas)
print('Equation size inheritance, direct formatting and run overrides passed.')

assert all(f.find('.//m:mo', ns).text == ',' for f in formulas)
assert all(f.find('.//m:mn', ns).text == '1.5' for f in formulas)
print('Math punctuation and decimal number classification passed.')

with zipfile.ZipFile(target) as package:
    formula = E.fromstring(package.read('Object 4/content.xml'))
    assert formula.find('.//m:munderover', ns) is not None
    assert formula.find('.//m:mfenced', ns).get('separators') == ''
    content = E.fromstring(package.read('content.xml'))
    odf = {'t': 'urn:oasis:names:tc:opendocument:xmlns:text:1.0', 's': 'urn:oasis:names:tc:opendocument:xmlns:style:1.0', 'f': 'urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0'}
    paragraph = content.findall('.//t:p', odf)[-1]
    style_name = paragraph.get('{'+odf['t']+'}style-name')
    styles = content.findall('.//s:style', odf)
    style = next(s for s in styles if s.get('{'+odf['s']+'}name') == style_name)
    assert style.find('s:paragraph-properties', odf).get('{'+odf['f']+'}text-align') == 'center'
print('Display alignment, visible sum limits and empty separators passed.')
