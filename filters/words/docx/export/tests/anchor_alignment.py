#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Check that page-relative ODF frame alignment survives DOCX export."""
import base64
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
WP = '{http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing}'


def fixture(path):
    with ZipFile(path, 'w') as z:
        z.writestr('mimetype', 'application/vnd.oasis.opendocument.text')
        z.writestr('META-INF/manifest.xml', '''<manifest:manifest
            xmlns:manifest="urn:oasis:names:tc:opendocument:xmlns:manifest:1.0">
            <manifest:file-entry manifest:full-path="/" manifest:media-type="application/vnd.oasis.opendocument.text"/>
            <manifest:file-entry manifest:full-path="content.xml" manifest:media-type="text/xml"/>
            <manifest:file-entry manifest:full-path="Pictures/cover.png" manifest:media-type="image/png"/>
            </manifest:manifest>''')
        z.writestr('meta.xml', f'<office:document-meta {ODF}><office:meta/></office:document-meta>')
        z.writestr('Pictures/cover.png', base64.b64decode(
            'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGP4//8/AAX+Av4N70a4AAAAAElFTkSuQmCC'))
        # The graphic style carries the alignment; svg:x/svg:y are both zero,
        # as Word writes them for an anchored cover image.
        z.writestr('content.xml', f'''<office:document-content {ODF} office:version="1.2">
            <office:automatic-styles>
            <style:style style:name="cover" style:family="graphic"><style:graphic-properties
                style:horizontal-rel="page" style:horizontal-pos="from-left"
                style:vertical-rel="page" style:vertical-pos="bottom"
                style:wrap="run-through" style:run-through="background"/></style:style>
            <style:style style:name="body" style:family="graphic"><style:graphic-properties
                style:horizontal-rel="page-content" style:horizontal-pos="from-left"
                style:vertical-rel="paragraph" style:vertical-pos="from-top"
                style:wrap="parallel"/></style:style>
            </office:automatic-styles><office:body><office:text>
            <text:p><draw:frame draw:style-name="cover" text:anchor-type="paragraph"
                svg:x="0cm" svg:y="0cm" svg:width="20cm" svg:height="14cm">
                <draw:image xlink:href="Pictures/cover.png" xlink:type="simple"/>
            </draw:frame></text:p>
            <text:p><draw:frame draw:style-name="body" text:anchor-type="char"
                svg:x="0.802in" svg:y="0.069in" svg:width="1in" svg:height="1in">
                <draw:image xlink:href="Pictures/cover.png" xlink:type="simple"/>
            </draw:frame></text:p>
            </office:text></office:body></office:document-content>''')
        z.writestr('styles.xml', f'''<office:document-styles {ODF} office:version="1.2">
            <office:styles/><office:automatic-styles>
            <style:page-layout style:name="page"><style:page-layout-properties
                fo:page-width="595pt" fo:page-height="842pt" fo:margin="36pt"/>
            </style:page-layout></office:automatic-styles><office:master-styles>
            <style:master-page style:name="Standard" style:page-layout-name="page"/>
            </office:master-styles></office:document-styles>''')


def run():
    with tempfile.TemporaryDirectory(prefix='docx-anchor-alignment-') as directory:
        directory = Path(directory)
        env = environment()
        env['XDG_CONFIG_HOME'] = str(directory / 'config')
        env['XDG_CACHE_HOME'] = str(directory / 'cache')
        source = directory / 'input.odt'
        fixture(source)
        docx = directory / 'output.docx'
        result = subprocess.run([ROOT / 'build/bin/calligraconverter', '--batch', str(source), str(docx)],
                                env=env, capture_output=True, text=True, timeout=60)
        assert result.returncode == 0, result.stdout + result.stderr
        with ZipFile(docx) as z:
            document = E.fromstring(z.read('word/document.xml'))
        anchors = document.findall(f'.//{WP}anchor')
        assert len(anchors) == 2, len(anchors)
        cover = next(a for a in anchors
                     if a.find(f'{WP}positionV').get('relativeFrom') == 'page')
        vertical = cover.find(f'{WP}positionV')
        assert vertical.find(f'{WP}align').text == 'bottom', E.tostring(vertical)
        assert vertical.find(f'{WP}posOffset') is None

        # A body frame anchored to its paragraph must keep that vertical base;
        # exporting it as page-relative pins it to the top of the page.
        body = next(a for a in anchors
                    if a.find(f'{WP}positionH').get('relativeFrom') == 'margin')
        vertical = body.find(f'{WP}positionV')
        assert vertical.get('relativeFrom') == 'paragraph', E.tostring(vertical)
        assert vertical.find(f'{WP}align') is None
        assert vertical.find(f'{WP}posOffset') is not None
        print('PASS: frame alignment and paragraph-relative basis survive DOCX export')


if __name__ == '__main__':
    run()
