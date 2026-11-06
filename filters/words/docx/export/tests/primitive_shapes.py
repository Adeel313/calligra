#!/usr/bin/env python3
"""ODF line and rectangle primitives must remain editable in DOCX."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from zipfile import ZipFile
import xml.etree.ElementTree as ET

W = '{http://schemas.openxmlformats.org/wordprocessingml/2006/main}'
A = '{http://schemas.openxmlformats.org/drawingml/2006/main}'
WPS = '{http://schemas.microsoft.com/office/word/2010/wordprocessingShape}'
DRAW = '{urn:oasis:names:tc:opendocument:xmlns:drawing:1.0}'


def convert(source, target):
    result = subprocess.run([sys.argv[1], '--batch', str(source), str(target)],
                            env={**os.environ, 'QT_QPA_PLATFORM': 'offscreen'},
                            capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr


with tempfile.TemporaryDirectory(prefix='docx-primitives-') as directory:
    directory = Path(directory)
    source = directory / 'primitives.odt'
    docx = directory / 'primitives.docx'
    reopened = directory / 'reopened.odt'
    with ZipFile(source, 'w') as package:
        package.writestr('mimetype', 'application/vnd.oasis.opendocument.text')
        package.writestr('meta.xml', '''<office:document-meta xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0">
<office:meta/></office:document-meta>''')
        package.writestr('content.xml', '''<office:document-content
xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0"
xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"
xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0"
xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0"
xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"
xmlns:xlink="http://www.w3.org/1999/xlink"><office:automatic-styles>
<style:style style:name="shape" style:family="graphic"><style:graphic-properties
draw:fill="none" draw:stroke="solid" svg:stroke-color="#224466"/></style:style>
</office:automatic-styles><office:body><office:text><text:p>Editable shapes:
<draw:rect draw:name="rectangle" draw:style-name="shape" text:anchor-type="as-char"
svg:x="10pt" svg:y="12pt" svg:width="80pt" svg:height="40pt"/>
<draw:line draw:name="line" draw:style-name="shape" text:anchor-type="as-char"
svg:x1="15pt" svg:y1="70pt" svg:x2="95pt" svg:y2="90pt"/>
</text:p></office:text></office:body></office:document-content>''')
        package.writestr('styles.xml', '''<office:document-styles
xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0"><office:styles/>
<office:automatic-styles/><office:master-styles/></office:document-styles>''')
        package.writestr('META-INF/manifest.xml', '''<manifest:manifest
xmlns:manifest="urn:oasis:names:tc:opendocument:xmlns:manifest:1.0">
<manifest:file-entry manifest:full-path="/" manifest:media-type="application/vnd.oasis.opendocument.text"/>
<manifest:file-entry manifest:full-path="content.xml" manifest:media-type="text/xml"/>
<manifest:file-entry manifest:full-path="styles.xml" manifest:media-type="text/xml"/>
</manifest:manifest>''')

    convert(source, docx)
    with ZipFile(docx) as package:
        document = ET.fromstring(package.read('word/document.xml'))
    presets = [shape.get('prst') for shape in document.findall(f'.//{A}prstGeom')]
    assert presets == ['rect', 'line'], presets
    assert len(document.findall(f'.//{WPS}wsp')) == 2

    convert(docx, reopened)
    with ZipFile(reopened) as package:
        content = ET.fromstring(package.read('content.xml'))
    assert len(content.findall(f'.//{DRAW}custom-shape')) == 2
    print('PASS: rectangles and lines remain editable through DOCX export and import')
