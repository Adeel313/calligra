#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Check OOXML font deobfuscation, ODF packaging and actual font selection."""
import argparse
import io
import os
from pathlib import Path
import subprocess
import zipfile

from docx import Document
from docx.shared import Pt
from fontTools.ttLib import TTFont
from lxml import etree as E
import pymupdf

ROOT = Path(__file__).resolve().parents[5]
W = 'http://schemas.openxmlformats.org/wordprocessingml/2006/main'
R = 'http://schemas.openxmlformats.org/officeDocument/2006/relationships'
FAMILY = 'Calligra Embedded Regression'
KEY = '00112233445566778899AABBCCDDEEFF'


def check(output):
    output.mkdir(parents=True, exist_ok=False)
    doc = Document()
    doc.styles['Normal'].font.name = FAMILY
    doc.styles['Normal'].font.size = Pt(12)
    doc.add_paragraph('Embedded regular')
    doc.add_paragraph().add_run('Embedded italic').italic = True
    source = output / 'embedded.docx'
    doc.save(source)
    with zipfile.ZipFile(source) as package:
        entries = {name: package.read(name) for name in package.namelist()}
    table = E.fromstring(entries['word/fontTable.xml'])
    face = E.SubElement(table, '{%s}font' % W, {'{%s}name' % W: FAMILY})
    rels = E.Element('Relationships', xmlns='http://schemas.openxmlformats.org/package/2006/relationships')
    expected = {}
    for index, (style, query) in enumerate([('Regular', 'DejaVu Sans:style=Book'), ('Italic', 'DejaVu Sans:style=Oblique')], 1):
        path = subprocess.check_output(['fc-match', '-f', '%{file}', query], text=True)
        font = TTFont(path)
        # A unique family makes this test fail if the loader substitutes a
        # system font instead of registering the embedded data.
        for record in font['name'].names:
            if record.nameID in (1, 4, 6, 16):
                name = FAMILY.replace(' ', '') + style if record.nameID == 6 else FAMILY
                if record.nameID == 4: name += ' ' + style
                record.string = name.encode(record.getEncoding())
        data = io.BytesIO()
        font.save(data)
        raw = data.getvalue()
        expected['Fonts/font%d.ttf' % index] = raw
        obfuscated = bytearray(raw)
        key = bytes.fromhex(KEY)[::-1]
        for i in range(32): obfuscated[i] ^= key[i % 16]
        entries['word/fonts/font%d.odttf' % index] = bytes(obfuscated)
        E.SubElement(face, '{%s}embed%s' % (W, style),
                     {'{%s}id' % R: 'font%d' % index, '{%s}fontKey' % W: '{' + KEY + '}'})
        E.SubElement(rels, 'Relationship', Id='font%d' % index,
                     Type=R + '/font', Target='fonts/font%d.odttf' % index)
    entries['word/fontTable.xml'] = E.tostring(table)
    entries['word/_rels/fontTable.xml.rels'] = E.tostring(rels)
    types = E.fromstring(entries['[Content_Types].xml'])
    E.SubElement(types, '{%s}Default' % types.nsmap[None], Extension='odttf',
                 ContentType='application/vnd.openxmlformats-officedocument.obfuscatedFont')
    entries['[Content_Types].xml'] = E.tostring(types)
    with zipfile.ZipFile(source, 'w') as package:
        for name, data in entries.items(): package.writestr(name, data)
    install = ROOT / 'build-opt/local-install'
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', XDG_CONFIG_HOME=str(output / 'config'),
               LD_LIBRARY_PATH=str(ROOT / 'build-opt/bin'),
               QT_PLUGIN_PATH=str(install / 'lib/x86_64-linux-gnu/qt6/plugins'),
               XDG_DATA_DIRS=str(install / 'share') + ':/usr/local/share:/usr/share')
    odt = output / 'embedded.odt'
    pdf = output / 'embedded.pdf'
    with (output / 'convert.log').open('w') as log:
        subprocess.run([ROOT / 'build-opt/bin/calligraconverter', '--batch', source, odt],
                       env=env, stdout=log, stderr=log, check=True, timeout=60)
    with zipfile.ZipFile(odt) as package:
        for name, raw in expected.items(): assert package.read(name) == raw, name
        styles = E.fromstring(package.read('styles.xml'))
        uris = styles.xpath('//*[local-name()="font-face-uri"]/@*[local-name()="href"]')
        assert set(expected).issubset(uris), uris
        manifest = package.read('META-INF/manifest.xml')
        for name in expected: assert name.encode() in manifest
    with (output / 'export.log').open('w') as log:
        subprocess.run([ROOT / 'run-words.sh', '--export-pdf', '--export-filename', pdf, source],
                       env=env, stdout=log, stderr=log, check=True, timeout=90)
    with pymupdf.open(pdf) as rendered:
        spans = [s for p in rendered for b in p.get_text('dict')['blocks']
                 for line in b.get('lines', []) for s in line['spans'] if 'Embedded' in s['text']]
        assert len(spans) == 2, spans
        assert all('CalligraEmbeddedRegression' in s['font'] for s in spans), spans
        assert spans[0]['font'] != spans[1]['font'], spans
    print('PASS: embedded regular/italic fonts preserved, decoded and selected for rendering')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    check(parser.parse_args().output.resolve())
