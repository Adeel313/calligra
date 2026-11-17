#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Verify borders on empty cells and merged-cell edges in native XLSX export."""
import argparse
import importlib.util
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET
import zipfile
from openpyxl import load_workbook

ROOT = Path(__file__).resolve().parents[4]
S = '{http://schemas.openxmlformats.org/spreadsheetml/2006/main}'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    out = parser.parse_args().output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    spec = importlib.util.spec_from_file_location(
        'options', ROOT / 'filters/words/docx/export/tests/export_options.py')
    options = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(options)
    styles = []
    for name, props in [('All', 'fo:border="1pt solid #ff0000"'),
                        ('Top', 'fo:border-top="1pt solid #008000"'),
                        ('TopRight', 'fo:border-top="1pt solid #008000" fo:border-right="1pt solid #008000"'),
                        ('LeftBottom', 'fo:border-left="1pt solid #008000" fo:border-bottom="1pt solid #008000"'),
                        ('Bottom', 'fo:border-bottom="1pt solid #008000"'),
                        ('RightBottom', 'fo:border-right="1pt solid #008000" fo:border-bottom="1pt solid #008000"')]:
        styles.append(f'<style:style style:name="{name}" style:family="table-cell"><style:table-cell-properties {props}/></style:style>')
    content = f'''<office:document-content
xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0"
xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0"
xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"
xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" office:version="1.2">
<office:automatic-styles>{''.join(styles)}</office:automatic-styles>
<office:body><office:spreadsheet><table:table table:name="Borders">
<table:table-row>
<table:table-cell office:value-type="string"><text:p>Header</text:p></table:table-cell>
<table:table-cell table:style-name="All"/>
<table:table-cell/>
<table:table-cell office:value-type="string"><text:p>After gap</text:p></table:table-cell>
</table:table-row>
<table:table-row>
<table:table-cell table:style-name="All" table:number-columns-spanned="3" table:number-rows-spanned="2" office:value-type="string"><text:p>Merged</text:p></table:table-cell>
<table:covered-table-cell table:style-name="Top"/>
<table:covered-table-cell table:style-name="TopRight"/>
<table:table-cell/>
<table:table-cell table:style-name="All" table:number-columns-spanned="3" table:number-rows-spanned="2" office:value-type="string"><text:p>Master border only</text:p></table:table-cell>
<table:covered-table-cell/><table:covered-table-cell/>
</table:table-row>
<table:table-row>
<table:covered-table-cell table:style-name="LeftBottom"/>
<table:covered-table-cell table:style-name="Bottom"/>
<table:covered-table-cell table:style-name="RightBottom"/>
<table:table-cell/><table:covered-table-cell/><table:covered-table-cell/><table:covered-table-cell/>
</table:table-row>
<table:table-row>
<table:table-cell/>
<table:table-cell table:style-name="All"/>
<table:table-cell/>
<table:table-cell office:value-type="float" office:value="42"/>
</table:table-row>
</table:table></office:spreadsheet></office:body></office:document-content>'''
    source, target = out / 'borders.ods', out / 'borders.xlsx'
    with zipfile.ZipFile(source, 'w') as package:
        package.writestr('mimetype', 'application/vnd.oasis.opendocument.spreadsheet')
        package.writestr('content.xml', content)
        package.writestr('META-INF/manifest.xml', '''<manifest:manifest xmlns:manifest="urn:oasis:names:tc:opendocument:xmlns:manifest:1.0">
<manifest:file-entry manifest:full-path="/" manifest:media-type="application/vnd.oasis.opendocument.spreadsheet"/>
<manifest:file-entry manifest:full-path="content.xml" manifest:media-type="text/xml"/>
</manifest:manifest>''')
    with (out / 'export.log').open('w') as log:
        subprocess.run([ROOT / 'build/bin/calligraconverter', '--batch', source, target],
                       env=options.environment(), stdout=log, stderr=log, check=True, timeout=120)
    with zipfile.ZipFile(target) as package:
        sheet = ET.fromstring(package.read('xl/worksheets/sheet1.xml'))
        styles = ET.fromstring(package.read('xl/styles.xml'))
    cells = {c.attrib['r']: c for c in sheet.findall('.//' + S + 'c')}
    xfs = list(styles.find(S + 'cellXfs'))
    borders = list(styles.find(S + 'borders'))
    expected = {'B1': ('left', 'right', 'top', 'bottom'),
                'B2': ('top',), 'C2': ('top', 'right'),
                'A3': ('left', 'bottom'), 'B3': ('bottom',),
                'C3': ('right', 'bottom'),
                'F2': ('top',), 'G2': ('top', 'right'),
                'E3': ('left', 'bottom'), 'F3': ('bottom',), 'G3': ('right', 'bottom'),
                'B4': ('left', 'right', 'top', 'bottom')}
    for address, sides in expected.items():
        assert address in cells, ('Missing styled empty cell', address)
        cell = cells[address]
        border = borders[int(xfs[int(cell.attrib['s'])].attrib['borderId'])]
        for side in sides:
            assert border.find(S + side).attrib.get('style'), (address, side, ET.tostring(border))
        assert cell.find(S + 'v') is None and cell.find(S + 'is') is None, address
        expected_color = 'FF008000' if address in ('B2', 'C2', 'A3', 'B3', 'C3') else 'FFFF0000'
        for side in sides:
            assert border.find(S + side + '/' + S + 'color').attrib['rgb'] == expected_color, (address, side)
    assert 'A2:C3' in [m.attrib['ref'] for m in sheet.findall('.//' + S + 'mergeCell')]
    workbook = load_workbook(target)
    assert workbook.active['B1'].value is None
    assert workbook.active['D1'].value == 'After gap'
    assert workbook.active['D4'].value == 42
    for side in ('left', 'right', 'top', 'bottom'):
        assert getattr(workbook.active['B1'].border, side).style
    print(f'PASS: empty cells and merged perimeter retain borders; artifacts: {out}')


if __name__ == '__main__':
    main()
