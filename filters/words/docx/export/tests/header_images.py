#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Check header logo positioning across two DOCX export/import cycles."""
import base64
import re
from pathlib import Path
import subprocess
import tempfile
import xml.etree.ElementTree as E
from zipfile import ZipFile

from export_options import ROOT, environment

ODF = '''xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0"
xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"
xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0"
xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"
xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0"
xmlns:xlink="http://www.w3.org/1999/xlink"'''
W = '{http://schemas.openxmlformats.org/wordprocessingml/2006/main}'
WP = '{http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing}'
SVG = '{urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0}'
DRAW = '{urn:oasis:names:tc:opendocument:xmlns:drawing:1.0}'


def points(value):
    number, unit = re.fullmatch(r'([-+\d.]+)(pt|cm|mm|in)', value).groups()
    return float(number) * {'pt': 1, 'cm': 72 / 2.54, 'mm': 72 / 25.4, 'in': 72}[unit]


def fixture(path):
    with ZipFile(path, 'w') as z:
        z.writestr('mimetype', 'application/vnd.oasis.opendocument.text')
        z.writestr('META-INF/manifest.xml', '''<manifest:manifest
            xmlns:manifest="urn:oasis:names:tc:opendocument:xmlns:manifest:1.0">
            <manifest:file-entry manifest:full-path="/" manifest:media-type="application/vnd.oasis.opendocument.text"/>
            <manifest:file-entry manifest:full-path="content.xml" manifest:media-type="text/xml"/>
            <manifest:file-entry manifest:full-path="styles.xml" manifest:media-type="text/xml"/>
            <manifest:file-entry manifest:full-path="Pictures/logo.png" manifest:media-type="image/png"/>
            </manifest:manifest>''')
        z.writestr('meta.xml', f'<office:document-meta {ODF}><office:meta/></office:document-meta>')
        z.writestr('Pictures/logo.png', base64.b64decode(
            'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aZ1cAAAAASUVORK5CYII='))
        z.writestr('content.xml', f'''<office:document-content {ODF} office:version="1.2">
            <office:automatic-styles/><office:body><office:text>
            <text:p>Body sentinel</text:p></office:text></office:body></office:document-content>''')
        z.writestr('styles.xml', f'''<office:document-styles {ODF} office:version="1.2">
            <office:styles/><office:automatic-styles>
            <style:style style:name="logo" style:family="graphic"><style:graphic-properties
                style:horizontal-rel="frame" style:horizontal-pos="from-left"
                style:vertical-rel="frame" style:vertical-pos="from-top"
                style:wrap="parallel" style:flow-with-text="true"/></style:style>
            <style:style style:name="title" style:family="paragraph">
                <style:paragraph-properties fo:text-align="center"/>
                <style:text-properties fo:font-size="17pt" fo:color="#226633"/>
            </style:style>
            <style:page-layout style:name="page"><style:page-layout-properties
                fo:page-width="612pt" fo:page-height="792pt" fo:margin="36pt"/>
                <style:header-style><style:header-footer-properties fo:min-height="80pt"/>
                </style:header-style></style:page-layout>
            </office:automatic-styles><office:master-styles>
            <style:master-page style:name="Standard" style:page-layout-name="page"><style:header>
            <text:p text:style-name="title"><draw:frame draw:style-name="logo" text:anchor-type="char"
                svg:x="12pt" svg:y="5pt" svg:width="60pt" svg:height="70pt">
                <draw:image xlink:href="Pictures/logo.png" xlink:type="simple"/>
            </draw:frame>Header sentinel</text:p></style:header></style:master-page>
            </office:master-styles></office:document-styles>''')


def run():
    with tempfile.TemporaryDirectory(prefix='docx-header-images-') as directory:
        directory = Path(directory)
        env = environment()
        env['XDG_CONFIG_HOME'] = str(directory / 'config')
        env['XDG_CACHE_HOME'] = str(directory / 'cache')
        source = directory / 'input.odt'
        fixture(source)
        for cycle in range(2):
            docx = directory / f'cycle-{cycle}.docx'
            odt = directory / f'cycle-{cycle}.odt'
            for first, last in [(source, docx), (docx, odt)]:
                result = subprocess.run([ROOT / 'build/bin/calligraconverter', '--batch', first, last],
                                        env=env, capture_output=True, text=True, timeout=60)
                assert result.returncode == 0, result.stdout + result.stderr
            with ZipFile(docx) as z:
                header = E.fromstring(z.read(next(n for n in z.namelist()
                                                 if n.startswith('word/header') and n.endswith('.xml'))))
                anchor = header.find(f'.//{WP}anchor')
                assert anchor is not None
                for axis, reference, offset in [('H', 'margin', 12 * 12700), ('V', 'paragraph', 5 * 12700)]:
                    position = anchor.find(f'{WP}position{axis}')
                    assert position.get('relativeFrom') == reference, E.tostring(position)
                    assert abs(int(position.find(f'{WP}posOffset').text) - offset) < 400
                assert anchor.find(f'{WP}wrapSquare') is not None
                assert 'Header sentinel' in ''.join(header.itertext())
                styles = E.fromstring(z.read('word/styles.xml'))
                assert any(n.get(W + 'val') == '34' for n in styles.iter(W + 'sz'))
                assert any(n.get(W + 'val', '').lower() == '226633' for n in styles.iter(W + 'color'))
                document = E.fromstring(z.read('word/document.xml'))
                margins = document.find(f'.//{W}pgMar')
                assert margins.get(W + 'header') == '720'
                assert margins.get(W + 'top') == '2320'
            with ZipFile(odt) as z:
                styles = E.fromstring(z.read('styles.xml'))
                frame = styles.find(f'.//{DRAW}frame')
                assert frame is not None
                assert abs(points(frame.get(SVG + 'x')) - 12) < 0.03
                assert abs(points(frame.get(SVG + 'y')) - 5) < 0.03
            source = odt
            print(f'PASS: header image DOCX cycle {cycle + 1}')


if __name__ == '__main__':
    run()
