#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Compare native XLSX numeric precision with LibreOffice on the same ODS.

Run with /usr/bin/python3; requires openpyxl and the installed native plugin.
Artifacts and both converter logs are retained in --output.
"""
import argparse
import importlib.util
import subprocess
import zipfile
from pathlib import Path
from openpyxl import load_workbook

ROOT = Path(__file__).resolve().parents[4]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    spec = importlib.util.spec_from_file_location('options', ROOT / 'filters/words/docx/export/tests/export_options.py')
    options = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(options)
    env = options.environment()
    cases = [('number', 0, False, '0'), ('number', 4, False, '0.0000'),
             ('number', 3, True, '#,##0.000'), ('percentage', 3, False, '0.000%'),
             ('scientific', 4, False, '0.0000E+00'), ('number', 4, False, '0.0000')]
    ns = ('xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" '
          'xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" '
          'xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0" '
          'xmlns:number="urn:oasis:names:tc:opendocument:xmlns:datastyle:1.0"')
    styles, cells = [], []
    for i, (kind, precision, grouped, _) in enumerate(cases):
        element = 'scientific-number' if kind == 'scientific' else 'number'
        family = 'percentage' if kind == 'percentage' else 'number'
        exponent = ' number:min-exponent-digits="2"' if kind == 'scientific' else ''
        percent = '<number:text>%</number:text>' if kind == 'percentage' else ''
        styles.append(f'<number:{family}-style style:name="N{i}"><number:{element} number:decimal-places="{precision}" number:min-integer-digits="1" number:grouping="{str(grouped).lower()}"{exponent}/>{percent}</number:{family}-style>'
                      f'<style:style style:name="C{i}" style:family="table-cell" style:data-style-name="N{i}"/>')
        cells.append(f'<table:table-cell table:style-name="C{i}" office:value-type="float" office:value="1234.56789"/>')
    source = out / 'precision.ods'
    with zipfile.ZipFile(source, 'w') as package:
        package.writestr('mimetype', 'application/vnd.oasis.opendocument.spreadsheet')
        package.writestr('content.xml', f'<office:document-content {ns} office:version="1.2"><office:automatic-styles>{"".join(styles)}</office:automatic-styles><office:body><office:spreadsheet><table:table table:name="Precision"><table:table-row>{"".join(cells)}</table:table-row></table:table></office:spreadsheet></office:body></office:document-content>')
        package.writestr('META-INF/manifest.xml', '<manifest:manifest xmlns:manifest="urn:oasis:names:tc:opendocument:xmlns:manifest:1.0"><manifest:file-entry manifest:full-path="/" manifest:media-type="application/vnd.oasis.opendocument.spreadsheet"/><manifest:file-entry manifest:full-path="content.xml" manifest:media-type="text/xml"/></manifest:manifest>')
    for name, command in [
        ('native', [ROOT / 'build/bin/calligraconverter', '--batch', source, out / 'native.xlsx']),
        ('reference', ['/usr/bin/python3', ROOT / 'filters/stage/pptx/tests/libreoffice_reference.py', source, out / 'reference.xlsx'])]:
        with (out / (name + '.log')).open('w') as log:
            subprocess.run(command, env=env, stdout=log, stderr=log, check=True, timeout=120)
    native = load_workbook(out / 'native.xlsx').active
    reference = load_workbook(out / 'reference.xlsx').active
    for i, (_, _, _, expected) in enumerate(cases, 1):
        actual, ref = native.cell(1, i), reference.cell(1, i)
        assert actual.value == ref.value == 1234.56789, (i, actual.value, ref.value)
        assert actual.number_format.replace('#,###0', '#,##0') == expected, (i, actual.number_format, expected)
        # LibreOffice can add an optional leading '#' in scientific notation.
        assert ref.number_format in (expected, '#' + expected if 'E+' in expected else expected), (i, ref.number_format, expected)
    print(f'PASS: {len(cases)} numeric formats and values match LibreOffice; artifacts: {out}')


if __name__ == '__main__':
    main()
