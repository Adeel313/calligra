#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Export a minimal ODS and verify that every XLSX part has a content type.

Run with /usr/bin/python3 against the locally built and installed plugin.
The --output directory must be new. Conversion artifacts and logs are retained.
"""
import argparse
import importlib.util
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET
import zipfile

ROOT = Path(__file__).resolve().parents[4]
CT = '{http://schemas.openxmlformats.org/package/2006/content-types}'


def check_package(path):
    with zipfile.ZipFile(path) as package:
        assert package.testzip() is None, 'Corrupt ZIP entry'
        names = package.namelist()
        assert 'mimetype' not in names, 'ODF mimetype entry in XLSX package'
        types = ET.fromstring(package.read('[Content_Types].xml'))
        defaults = {e.attrib['Extension']: e.attrib['ContentType']
                    for e in types.findall(CT + 'Default')}
        overrides = {e.attrib['PartName']: e.attrib['ContentType']
                     for e in types.findall(CT + 'Override')}
        for name in names:
            if name.endswith('/') or name == '[Content_Types].xml':
                continue
            extension = name.rsplit('.', 1)[-1] if '.' in name else ''
            assert overrides.get('/' + name) or defaults.get(extension), (
                'Package part has no content type', name)
            if name.endswith(('.xml', '.rels')):
                root = ET.fromstring(package.read(name))
                if name.startswith('xl/worksheets/') and name.endswith('.xml'):
                    ns = '{http://schemas.openxmlformats.org/spreadsheetml/2006/main}'
                    for column in root.findall(ns + 'cols/' + ns + 'col'):
                        first, last = int(column.attrib['min']), int(column.attrib['max'])
                        assert 1 <= first <= last <= 16384, (
                            'Column formatting exceeds the Excel grid', name, column.attrib)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    source, target = out / 'package.ods', out / 'package.xlsx'
    with zipfile.ZipFile(source, 'w') as package:
        package.writestr('mimetype', 'application/vnd.oasis.opendocument.spreadsheet')
        package.writestr('content.xml', '''<office:document-content
xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0"
xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0"
xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" office:version="1.2">
<office:automatic-styles>
<style:style style:name="Wide" style:family="table-column">
<style:table-column-properties style:column-width="75pt"/>
</style:style></office:automatic-styles>
<office:body><office:spreadsheet><table:table table:name="Package">
<table:table-column table:number-columns-repeated="995"/>
<table:table-column table:style-name="Wide" table:number-columns-repeated="31772"/>
<table:table-row><table:table-cell office:value-type="string"><text:p>Package test</text:p>
</table:table-cell></table:table-row></table:table></office:spreadsheet></office:body>
</office:document-content>''')
        package.writestr('META-INF/manifest.xml', '''<manifest:manifest
xmlns:manifest="urn:oasis:names:tc:opendocument:xmlns:manifest:1.0">
<manifest:file-entry manifest:full-path="/" manifest:media-type="application/vnd.oasis.opendocument.spreadsheet"/>
<manifest:file-entry manifest:full-path="content.xml" manifest:media-type="text/xml"/>
</manifest:manifest>''')
    spec = importlib.util.spec_from_file_location(
        'options', ROOT / 'filters/words/docx/export/tests/export_options.py')
    options = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(options)
    with (out / 'convert.log').open('w') as log:
        subprocess.run([ROOT / 'build/bin/calligraconverter', '--batch', source, target],
                       env=options.environment(), stdout=log, stderr=log,
                       check=True, timeout=120)
    check_package(target)
    print(f'PASS: every XLSX part has a content type; artifacts: {out}')


if __name__ == '__main__':
    main()
