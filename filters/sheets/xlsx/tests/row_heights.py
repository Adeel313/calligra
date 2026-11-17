#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Check that XLSX export preserves common and individually resized row heights.

Run against the locally built and installed filters with a new --output folder.
"""
import argparse
import importlib.util
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET
import zipfile

ROOT = Path(__file__).resolve().parents[4]
S = '{http://schemas.openxmlformats.org/spreadsheetml/2006/main}'
O = '{urn:oasis:names:tc:opendocument:xmlns:style:1.0}'
T = '{urn:oasis:names:tc:opendocument:xmlns:table:1.0}'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    out = parser.parse_args().output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    spec = importlib.util.spec_from_file_location(
        'options', ROOT / 'filters/words/docx/export/tests/export_options.py')
    options = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(options)
    heights = [18, 36, 36, 36, 36, 42, 15]
    styles = ''.join(f'<style:style style:name="R{i}" style:family="table-row">'
                     f'<style:table-row-properties style:row-height="{h}pt" style:use-optimal-row-height="false"/>'
                     '</style:style>' for i, h in enumerate(heights))
    rows = ''.join(f'<table:table-row table:style-name="R{i}">'
                   f'<table:table-cell office:value-type="string"><text:p>Row {i+1}</text:p>'
                   '</table:table-cell></table:table-row>' for i in range(len(heights)))
    source = out / 'heights.ods'
    with zipfile.ZipFile(source, 'w') as package:
        package.writestr('mimetype', 'application/vnd.oasis.opendocument.spreadsheet')
        package.writestr('content.xml', f'''<office:document-content
xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0"
xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0"
xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" office:version="1.2">
<office:automatic-styles>{styles}</office:automatic-styles>
<office:body><office:spreadsheet><table:table table:name="Heights">{rows}</table:table>
</office:spreadsheet></office:body></office:document-content>''')
        package.writestr('META-INF/manifest.xml', '''<manifest:manifest
xmlns:manifest="urn:oasis:names:tc:opendocument:xmlns:manifest:1.0">
<manifest:file-entry manifest:full-path="/" manifest:media-type="application/vnd.oasis.opendocument.spreadsheet"/>
<manifest:file-entry manifest:full-path="content.xml" manifest:media-type="text/xml"/>
</manifest:manifest>''')
    target = out / 'heights.xlsx'
    restored = out / 'restored.ods'
    def convert(source, target, log):
        with (out / log).open('w') as handle:
            subprocess.run([ROOT / 'build/bin/calligraconverter', '--batch', source, target],
                           env=options.environment(), stdout=handle, stderr=handle,
                           check=True, timeout=120)
    convert(source, target, 'export.log')
    with zipfile.ZipFile(target) as package:
        sheet = ET.fromstring(package.read('xl/worksheets/sheet1.xml'))
    for row, expected in zip(sheet.findall(S + 'sheetData/' + S + 'row'), heights):
        assert abs(float(row.attrib.get('ht', '0')) - expected) < 0.02, (row.attrib, expected)
        assert row.attrib.get('customHeight') == '1', row.attrib
    assert len(sheet.findall(S + 'sheetData/' + S + 'row')) == len(heights)
    assert sheet.find(S + 'sheetFormatPr') is not None
    convert(target, restored, 'import.log')
    with zipfile.ZipFile(restored) as package:
        content = ET.fromstring(package.read('content.xml'))
        style_heights = {s.attrib[O + 'name']: s.find(O + 'table-row-properties').attrib[O + 'row-height']
                         for s in content.iter(O + 'style') if s.find(O + 'table-row-properties') is not None}
        rows = list(content.iter(T + 'table-row'))
    for row, expected in zip(rows, heights):
        actual = style_heights[row.attrib[T + 'style-name']]
        assert actual.endswith('cm'), actual
        assert abs(float(actual[:-2]) * 72 / 2.54 - expected) < 0.02, (actual, expected)
    print(f'PASS: all {len(heights)} row heights survive XLSX export and reimport; artifacts: {out}')


if __name__ == '__main__':
    main()
