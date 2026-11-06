#!/usr/bin/env python3
"""Office preset shapes must remain editable through repeated DOCX saves.

Pass calligraconverter with the local shape/import/export plugin environment.
"""
from pathlib import Path
from zipfile import ZipFile
import os
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[5]
D = '{urn:oasis:names:tc:opendocument:xmlns:drawing:1.0}'
T = '{urn:oasis:names:tc:opendocument:xmlns:text:1.0}'
A = '{http://schemas.openxmlformats.org/drawingml/2006/main}'
W = '{http://schemas.openxmlformats.org/wordprocessingml/2006/main}'
NS = '''xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0"
xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"
xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0"
xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0"
xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"'''
with ZipFile(ROOT / 'filters/libmsooxml/doc/presetShapeDefinitions.xml.zip') as definitions:
    presets = [element.tag.split('}')[-1] for element in ET.fromstring(definitions.read('presetShapeDefinitions.xml'))]
assert len(presets) == 187


def convert(source, target):
    result = subprocess.run([sys.argv[1], '--batch', str(source), str(target)],
                            env={**os.environ, 'QT_QPA_PLATFORM': 'offscreen'},
                            capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr


with tempfile.TemporaryDirectory(prefix='docx-preset-shapes-') as directory:
    directory = Path(directory)
    source = directory / 'presets.odt'
    shapes = []
    for index, preset in enumerate(presets):
        # The preset identity is the export contract; the Word importer must
        # reconstruct the real geometry rather than flatten it to an image.
        shapes.append(f'''<text:p><draw:custom-shape draw:name="shape-{index}"
text:anchor-type="as-char" svg:width="120pt" svg:height="90pt">
<text:p>{preset} — اردو &amp; English</text:p>
<draw:enhanced-geometry draw:type="ooxml-{preset}" svg:viewBox="0 0 21600 21600"
draw:enhanced-path="M 0 0 L 21600 0 21600 21600 0 21600 Z"/>
</draw:custom-shape></text:p>''')
    with ZipFile(source, 'w') as package:
        package.writestr('mimetype', 'application/vnd.oasis.opendocument.text')
        package.writestr('meta.xml', f'<office:document-meta {NS}><office:meta/></office:document-meta>')
        package.writestr('content.xml', f'<office:document-content {NS}><office:automatic-styles/>'
                          f'<office:body><office:text>{"".join(shapes)}</office:text></office:body></office:document-content>')
        package.writestr('styles.xml', f'<office:document-styles {NS}><office:styles/><office:automatic-styles/>'
                          '<office:master-styles/></office:document-styles>')
        package.writestr('META-INF/manifest.xml', '''<manifest:manifest xmlns:manifest="urn:oasis:names:tc:opendocument:xmlns:manifest:1.0">
<manifest:file-entry manifest:full-path="/" manifest:media-type="application/vnd.oasis.opendocument.text"/>
<manifest:file-entry manifest:full-path="content.xml" manifest:media-type="text/xml"/>
<manifest:file-entry manifest:full-path="styles.xml" manifest:media-type="text/xml"/>
</manifest:manifest>''')
    for cycle in range(2):
        docx = directory / f'presets-{cycle}.docx'
        reopened = directory / f'reopened-{cycle}.odt'
        convert(source, docx)
        with ZipFile(docx) as package:
            root = ET.fromstring(package.read('word/document.xml'))
        assert [e.get('prst') for e in root.findall(f'.//{A}prstGeom')] == presets
        assert len(root.findall(f'.//{W}txbxContent')) == 187
        assert sum('اردو' in (t.text or '') for t in root.findall(f'.//{W}t')) == 187
        convert(docx, reopened)
        with ZipFile(reopened) as package:
            root = ET.fromstring(package.read('content.xml'))
        native = root.findall(f'.//{D}custom-shape')
        assert len(native) == 187
        assert [s.find(D + 'enhanced-geometry').get(D + 'type') for s in native] == ['ooxml-' + p for p in presets]
        assert sum('اردو' in (t.text or '') for t in root.findall(f'.//{T}p')) == 187
        # Reassigned guide names must not become recursive ODF equations.
        import re
        for shape in native:
            formulas = {e.get(D + 'name'): e.get(D + 'formula') for e in shape.findall(f'.//{D}equation')}
            def visit(name, active, complete):
                assert name not in active, f'circular guide in {shape.get(D + "name")}: {name}'
                if name in complete:
                    return
                active.add(name)
                for dependency in re.findall(r'\?([A-Za-z_][A-Za-z0-9_]*)', formulas.get(name, '')):
                    visit(dependency, active, complete)
                active.remove(name)
                complete.add(name)
            complete = set()
            for name in formulas:
                visit(name, set(), complete)
        source = reopened
        print(f'PASS: cycle {cycle + 1}: all 187 editable presets, Unicode text, and acyclic geometry guides')
