#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Verify embedded videos with NULL legacy links and missing video previews."""
import base64
import io
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile
import xml.etree.ElementTree as E

from pptx import Presentation
from pptx.util import Inches

ROOT = Path(__file__).resolve().parents[4]
P = 'http://schemas.openxmlformats.org/presentationml/2006/main'
A = 'http://schemas.openxmlformats.org/drawingml/2006/main'
R = 'http://schemas.openxmlformats.org/officeDocument/2006/relationships'
REL = 'http://schemas.openxmlformats.org/package/2006/relationships'
P14 = 'http://schemas.microsoft.com/office/powerpoint/2010/main'
DRAW = 'urn:oasis:names:tc:opendocument:xmlns:drawing:1.0'
XLINK = 'http://www.w3.org/1999/xlink'
VIDEO = b'regression embedded video payload'


def main():
    build = Path(os.environ.get('CALLIGRA_BUILD_DIR', ROOT / 'build-opt'))
    install = build / 'local-install'
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_LOGGING_RULES='*.debug=false',
               LD_LIBRARY_PATH=f'{build}/bin:{install}/lib/x86_64-linux-gnu',
               QT_PLUGIN_PATH=f'{build}/bin:{install}/lib/x86_64-linux-gnu/qt6/plugins',
               XDG_DATA_DIRS=f'{install}/share:/usr/share')
    with tempfile.TemporaryDirectory(prefix='pptx-media-') as tmp:
        tmp = Path(tmp)
        deck = Presentation()
        slide = deck.slides.add_slide(deck.slide_layouts[6])
        slide.shapes.add_picture(io.BytesIO(base64.b64decode(
            'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+a8H0AAAAASUVORK5CYII=')),
            Inches(1), Inches(1), Inches(2), Inches(2))
        original = tmp / 'original.pptx'
        deck.save(original)
        with zipfile.ZipFile(original) as z:
            parts = {name: z.read(name) for name in z.namelist()}
        for case in ('embedded', 'null', 'missing', 'internal', 'external'):
            source = tmp / f'{case}.pptx'
            root = E.fromstring(parts['ppt/slides/slide1.xml'])
            props = root.find(f'.//{{{P}}}pic/{{{P}}}nvPicPr/{{{P}}}nvPr')
            E.SubElement(props, f'{{{A}}}videoFile', {f'{{{R}}}link': 'rVideo'})
            rels = E.fromstring(parts['ppt/slides/_rels/slide1.xml.rels'])
            target = {'internal': '../media/video.mp4', 'missing': '../media/missing.mp4',
                      'external': 'https://example.com/video.mp4'}.get(case, 'NULL')
            attrs = dict(Id='rVideo', Type=R + '/video', Target=target)
            if case in ('embedded', 'null', 'external'):
                attrs['TargetMode'] = 'External'
            E.SubElement(rels, f'{{{REL}}}Relationship', attrs)
            if case == 'embedded':
                ext = E.SubElement(E.SubElement(props, f'{{{P}}}extLst'), f'{{{P}}}ext', {'uri': '{DAA4B4D4-6D71-4841-9C94-3DE7FCFB9230}'})
                E.SubElement(ext, f'{{{P14}}}media', {f'{{{R}}}embed': 'rMedia'})
                E.SubElement(rels, f'{{{REL}}}Relationship', dict(Id='rMedia', Type='http://schemas.microsoft.com/office/2007/relationships/media', Target='../media/video.mp4'))
            # Preserve python-pptx's namespace prefixes: the importer currently
            # uses qualified names for some PresentationML elements.
            E.register_namespace('', REL)
            E.register_namespace('p', P)
            E.register_namespace('a', A)
            E.register_namespace('r', R)
            E.register_namespace('p14', P14)
            with zipfile.ZipFile(source, 'w', zipfile.ZIP_DEFLATED) as z:
                for name, data in parts.items():
                    if name == 'ppt/slides/slide1.xml':
                        data = E.tostring(root)
                    elif name == 'ppt/slides/_rels/slide1.xml.rels':
                        data = E.tostring(rels)
                    z.writestr(name, data)
                if case in ('embedded', 'internal'):
                    z.writestr('ppt/media/video.mp4', VIDEO)
            output = tmp / f'{case}.odp'
            result = subprocess.run([build / 'bin/calligraconverter', '--batch', source, output],
                                    env=env, capture_output=True, timeout=60)
            assert result.returncode == 0 and output.exists(), result.stderr.decode()
            with zipfile.ZipFile(output) as z:
                content = E.fromstring(z.read('content.xml'))
                plugins = content.findall(f'.//{{{DRAW}}}plugin')
                assert content.findall(f'.//{{{DRAW}}}image'), f'{case}: preview lost'
                if case in ('embedded', 'internal'):
                    assert len(plugins) == 1, case
                    assert z.read(plugins[0].get(f'{{{XLINK}}}href')) == VIDEO
                elif case == 'external':
                    assert len(plugins) == 1
                    assert plugins[0].get(f'{{{XLINK}}}href') == target
                else:
                    assert not plugins, case
            print(f'PASS: {case}')


if __name__ == '__main__':
    main()
