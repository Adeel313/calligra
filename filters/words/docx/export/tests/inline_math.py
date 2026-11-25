#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Inline and packaged MathML must save as editable DOCX equations.

Pass calligraconverter in its normal local plugin environment.
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as E
from zipfile import ZipFile, ZIP_DEFLATED

ODF = 'urn:oasis:names:tc:opendocument:xmlns:'
MATH = 'http://www.w3.org/1998/Math/MathML'
W = '{http://schemas.openxmlformats.org/wordprocessingml/2006/main}'
M = '{http://schemas.openxmlformats.org/officeDocument/2006/math}'
NS = f'''xmlns:office="{ODF}office:1.0" xmlns:text="{ODF}text:1.0"
xmlns:draw="{ODF}drawing:1.0" xmlns:style="{ODF}style:1.0"
xmlns:svg="{ODF}svg-compatible:1.0" xmlns:xlink="http://www.w3.org/1999/xlink"
xmlns:eq="{MATH}"'''

def frame(name, contents):
    return f'''<draw:frame draw:name="{name}" text:anchor-type="as-char"
svg:width="80pt" svg:height="24pt"><draw:object>{contents}</draw:object></draw:frame>'''

fraction = '<eq:math><eq:mfrac><eq:mi>a</eq:mi><eq:mi>b</eq:mi></eq:mfrac></eq:math>'
radical = '<eq:math><eq:msqrt><eq:mi>c</eq:mi></eq:msqrt></eq:math>'
power = '<eq:math><eq:msup><eq:mi>x</eq:mi><eq:mn>2</eq:mn></eq:msup></eq:math>'
header = frame('header-formula', fraction)
content = f'''<office:document-content {NS} office:version="1.2"><office:body><office:text>
<text:p>Before first {frame('fraction', fraction)} after first.</text:p>
<text:p>Before second {frame('radical', radical)} after second.</text:p>
<text:p>Before third {frame('power', power)} after third.</text:p>
<text:p>Before linked <draw:frame draw:name="linked-formula" text:anchor-type="as-char"
svg:width="80pt" svg:height="24pt"><draw:object xlink:href="./Formula1"/></draw:frame> after linked.</text:p>
<text:p>Final sentinel</text:p></office:text></office:body></office:document-content>'''
styles = f'''<office:document-styles {NS} office:version="1.2"><office:styles/>
<office:automatic-styles><style:page-layout style:name="pm1"><style:page-layout-properties/></style:page-layout></office:automatic-styles>
<office:master-styles><style:master-page style:name="Standard" style:page-layout-name="pm1"><style:header>
<text:p>Header before {header} header after.</text:p></style:header></style:master-page></office:master-styles></office:document-styles>'''
manifest = f'''<manifest:manifest xmlns:manifest="{ODF}manifest:1.0" manifest:version="1.2">
<manifest:file-entry manifest:full-path="/" manifest:media-type="application/vnd.oasis.opendocument.text"/>
<manifest:file-entry manifest:full-path="content.xml" manifest:media-type="text/xml"/>
<manifest:file-entry manifest:full-path="styles.xml" manifest:media-type="text/xml"/>
<manifest:file-entry manifest:full-path="meta.xml" manifest:media-type="text/xml"/>
<manifest:file-entry manifest:full-path="Formula1/" manifest:media-type="application/vnd.oasis.opendocument.formula"/>
<manifest:file-entry manifest:full-path="Formula1/content.xml" manifest:media-type="text/xml"/>
</manifest:manifest>'''

with tempfile.TemporaryDirectory(prefix='docx-inline-math-') as directory:
    directory = Path(directory)
    source, target = directory / 'formulas.odt', directory / 'formulas.docx'
    with ZipFile(source, 'w', ZIP_DEFLATED) as package:
        package.writestr('mimetype', 'application/vnd.oasis.opendocument.text')
        package.writestr('content.xml', content)
        package.writestr('styles.xml', styles)
        package.writestr('meta.xml', f'<office:document-meta {NS} office:version="1.2"><office:meta/></office:document-meta>')
        package.writestr('META-INF/manifest.xml', manifest)
        package.writestr('Formula1/content.xml', f'<math xmlns="{MATH}"><mi>q</mi></math>')
    result = subprocess.run([sys.argv[1], '--batch', str(source), str(target)],
                            env={**os.environ, 'QT_QPA_PLATFORM': 'offscreen'},
                            capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    with ZipFile(target) as package:
        assert package.testzip() is None
        root = E.fromstring(package.read('word/document.xml'))
        assert len(root.findall('.//' + M + 'oMath')) == 4
        assert len(root.findall('.//' + M + 'f')) == 1
        assert len(root.findall('.//' + M + 'rad')) == 1
        assert len(root.findall('.//' + M + 'sSup')) == 1
        assert root.find('.//' + M + 'num/' + M + 'r/' + M + 't').text == 'a'
        assert root.find('.//' + M + 'den/' + M + 'r/' + M + 't').text == 'b'
        assert [n.text for n in root.findall('.//' + M + 't')] == ['a', 'b', 'c', 'x', '2', 'q']
        text = ''.join(n.text or '' for n in root.findall('.//' + W + 't'))
        for sentinel in ['Before first', 'after first.', 'Before second', 'after second.',
                         'Before third', 'after third.', 'Before linked', 'after linked.', 'Final sentinel']:
            assert sentinel in text, sentinel
        headers = [E.fromstring(package.read(name)) for name in package.namelist()
                   if name.startswith('word/header') and name.endswith('.xml')]
        assert sum(len(header.findall('.//' + M + 'oMath')) for header in headers) == 1
        assert any('header after.' in ''.join(n.text or '' for n in header.findall('.//' + W + 't')) for header in headers)
        assert not any(name.startswith('word/charts/') for name in package.namelist())
print('PASS: inline fraction, radical, power, packaged formula, header formula, and following text')
