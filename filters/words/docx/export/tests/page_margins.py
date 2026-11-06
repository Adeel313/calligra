#!/usr/bin/env python3
"""Integration regression: run with a calligraconverter path and its plugin environment."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from zipfile import ZipFile

NS = '''xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0"
xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"
xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"'''
W = '{http://schemas.openxmlformats.org/wordprocessingml/2006/main}'
CASES = [
    ('equal', 'fo:margin="72pt"', (1440, 1440, 1440, 1440)),
    ('unequal', 'fo:margin-top="36pt" fo:margin-right="54pt" '
     'fo:margin-bottom="72pt" fo:margin-left="90pt"', (720, 1080, 1440, 1800)),
    ('override', 'fo:margin="72pt" fo:margin-left="90pt"', (1440, 1440, 1440, 1800)),
    ('zero-left', 'fo:margin="72pt" fo:margin-left="0pt"', (1440, 1440, 1440, 0)),
]

with tempfile.TemporaryDirectory(prefix='docx-page-margins-') as directory:
    for name, margins, expected in CASES:
        source = Path(directory) / (name + '.odt')
        target = source.with_suffix('.docx')
        with ZipFile(source, 'w') as package:
            package.writestr('mimetype', 'application/vnd.oasis.opendocument.text')
            package.writestr('meta.xml', f'<office:document-meta {NS}><office:meta/></office:document-meta>')
            package.writestr('META-INF/manifest.xml', '''<manifest:manifest
xmlns:manifest="urn:oasis:names:tc:opendocument:xmlns:manifest:1.0">
<manifest:file-entry manifest:full-path="/" manifest:media-type="application/vnd.oasis.opendocument.text"/>
<manifest:file-entry manifest:full-path="content.xml" manifest:media-type="text/xml"/>
<manifest:file-entry manifest:full-path="styles.xml" manifest:media-type="text/xml"/>
</manifest:manifest>''')
            package.writestr('content.xml', f'''<office:document-content {NS} office:version="1.2">
<office:automatic-styles/><office:body><office:text><text:p>Page margin test</text:p>
</office:text></office:body></office:document-content>''')
            package.writestr('styles.xml', f'''<office:document-styles {NS} office:version="1.2">
<office:styles/><office:automatic-styles><style:page-layout style:name="pm1">
<style:page-layout-properties fo:page-width="612pt" fo:page-height="792pt" {margins}/>
</style:page-layout></office:automatic-styles><office:master-styles>
<style:master-page style:name="Standard" style:page-layout-name="pm1"/>
</office:master-styles></office:document-styles>''')
        result = subprocess.run([sys.argv[1], '--batch', str(source), str(target)],
                                env={**os.environ, 'QT_QPA_PLATFORM': 'offscreen'},
                                capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, result.stdout + result.stderr
        with ZipFile(target) as package:
            root = ET.fromstring(package.read('word/document.xml'))
        pgmar = root.find(f'.//{W}sectPr/{W}pgMar')
        assert pgmar is not None, f'{name}: missing page margins'
        actual = tuple(int(pgmar.attrib[W + side]) for side in ('top', 'right', 'bottom', 'left'))
        assert actual == expected, f'{name}: expected {expected}, got {actual}'
        print(f'PASS: {name}')
