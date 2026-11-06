#!/usr/bin/env python3
"""DOCX chart regression. Pass calligraconverter and its usual plugin environment."""
from io import BytesIO
from pathlib import Path
import os
import posixpath
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from zipfile import ZipFile

ODF = 'urn:oasis:names:tc:opendocument:xmlns:'
NS = {'chart': ODF + 'chart:1.0', 'table': ODF + 'table:1.0', 'office': ODF + 'office:1.0',
      'text': ODF + 'text:1.0', 'draw': ODF + 'drawing:1.0', 'style': ODF + 'style:1.0',
      'svg': ODF + 'svg-compatible:1.0', 'xlink': 'http://www.w3.org/1999/xlink'}
C = '{http://schemas.openxmlformats.org/drawingml/2006/chart}'
W = '{http://schemas.openxmlformats.org/wordprocessingml/2006/main}'
R = '{http://schemas.openxmlformats.org/officeDocument/2006/relationships}'
REL = '{http://schemas.openxmlformats.org/package/2006/relationships}'
S = '{http://schemas.openxmlformats.org/spreadsheetml/2006/main}'
FIXTURE = Path(__file__).resolve().parents[5] / 'words/tests/data/allcharts.odt'


def convert(source, target, succeeds=True):
    result = subprocess.run([sys.argv[1], '--batch', str(source), str(target)],
                            env={**os.environ, 'QT_QPA_PLATFORM': 'offscreen'},
                            capture_output=True, text=True, timeout=60)
    assert (result.returncode == 0) == succeeds, result.stdout + result.stderr
    return result


def odf_series(data):
    root = ET.fromstring(data)
    table = []
    for row in root.findall('.//table:table-row', NS):
        values = []
        for cell in row:
            if cell.tag not in (f"{{{NS['table']}}}table-cell", f"{{{NS['table']}}}covered-table-cell"):
                continue
            value = cell.get(f"{{{NS['office']}}}value")
            if value is None:
                value = cell.findtext('text:p', '', NS)
            values.extend([value] * int(cell.get(f"{{{NS['table']}}}number-columns-repeated", '1')))
        table.extend([values] * int(row.get(f"{{{NS['table']}}}number-rows-repeated", '1')))

    def values(address):
        coords = re.findall(r'\$?([A-Za-z]+)\$?([1-9][0-9]*)', address)
        assert coords, address
        def coord(pair):
            col = 0
            for c in pair[0].upper():
                col = col * 26 + ord(c) - ord('A') + 1
            return int(pair[1]) - 1, col - 1
        r1, c1 = coord(coords[0])
        r2, c2 = coord(coords[-1])
        return [table[r][c] for r in range(r1, r2 + 1) for c in range(c1, c2 + 1)]

    return [values(s.get(f"{{{NS['chart']}}}values-cell-range-address", ''))
            for s in root.findall('.//chart:series', NS)]


def verify_docx(path, expected):
    with ZipFile(path) as package:
        # Check every XML part and every embedded worksheet, not just object count.
        for name in package.namelist():
            if name.endswith(('.xml', '.rels')):
                ET.fromstring(package.read(name))
        charts = [n for n in package.namelist() if re.fullmatch(r'word/charts/chart\d+\.xml', n)]
        assert len(charts) == expected
        document = ET.fromstring(package.read('word/document.xml'))
        assert len(document.findall(f'.//{C}chart')) == expected
        assert not document.findall(f'.//{W}r/{W}r'), 'nested runs'
        cache_values = []
        for name in charts:
            chart = ET.fromstring(package.read(name))
            cache_values.append([[p.findtext(C + 'v', '') for p in s.findall(f'./{C}bubbleSize/{C}numRef/{C}numCache/{C}pt')]
                                 or [p.findtext(C + 'v', '') for p in s.findall(f'./{C}val/{C}numRef/{C}numCache/{C}pt')]
                                 or [p.findtext(C + 'v', '') for p in s.findall(f'./{C}yVal/{C}numRef/{C}numCache/{C}pt')]
                                 for s in chart.findall(f'.//{C}ser')])
            rels = ET.fromstring(package.read(posixpath.dirname(name) + '/_rels/' + posixpath.basename(name) + '.rels'))
            relation = chart.find(C + 'externalData').get(R + 'id')
            target = next(x.get('Target') for x in rels if x.get('Id') == relation)
            target = posixpath.normpath(posixpath.join(posixpath.dirname(name), target))
            with ZipFile(BytesIO(package.read(target))) as workbook:
                sheet = ET.fromstring(workbook.read('xl/worksheets/sheet1.xml'))
                assert sheet.findall(f'.//{S}c'), 'missing editable worksheet data'
                cells = {cell.get('r'): cell.findtext(S + 'v', cell.findtext(f'{S}is/{S}t', ''))
                         for cell in sheet.findall(f'.//{S}c')}
                for reference in chart.findall(f'.//{C}numRef') + chart.findall(f'.//{C}strRef'):
                    formula = reference.findtext(C + 'f')
                    column = re.search(r'!\$([A-Z]+)\$1:', formula).group(1)
                    for point in reference.findall(f'./{C}numCache/{C}pt') + reference.findall(f'./{C}strCache/{C}pt'):
                        address = column + str(int(point.get('idx')) + 1)
                        assert cells.get(address, '') == point.findtext(C + 'v', ''), (name, address)

        return cache_values


with tempfile.TemporaryDirectory(prefix='docx-charts-') as directory:
    directory = Path(directory)
    docx = directory / 'allcharts.docx'
    reopened = directory / 'reopened.odt'
    resaved = directory / 'resaved.docx'
    with ZipFile(FIXTURE) as package:
        expected_data = [odf_series(package.read(f'Object {i}/content.xml')) for i in range(1, 19)]
    convert(FIXTURE, docx)
    actual = verify_docx(docx, 18)
    expected_drawingml = [list(reversed(values)) if i in (6, 7, 8) else values for i, values in enumerate(expected_data)]
    assert actual == expected_drawingml, 'series values changed on export'
    convert(docx, reopened)
    with ZipFile(reopened) as package:
        chart_parts = [n for n in package.namelist() if re.fullmatch(r'Chart\d+/content.xml', n)]
        assert len(chart_parts) == 18
        reopened_data = [odf_series(package.read(f'Chart{i}/content.xml')) for i in range(1, 19)]
        assert reopened_data == expected_data, 'series values changed on reopening'
    convert(reopened, resaved)
    assert verify_docx(resaved, 18) == expected_drawingml, 'series values changed on second save'
    print('PASS: 18 charts retain their series data through export, reopen, and second save')

    # Keep one real chart from the fixture, then exercise both drawing placements,
    # sparse numeric data, Unicode labels and header-part relationship ownership.
    namespaces = ' '.join(f'xmlns:{k}="{v}"' for k, v in NS.items())
    with ZipFile(FIXTURE) as fixture:
        chart_data = fixture.read('Object 1/content.xml')
        styles = fixture.read('styles.xml')
    for placement in ('inline', 'floating', 'header', 'unsupported'):
        source = directory / (placement + '.odt')
        target = source.with_suffix('.docx')
        chart = ET.fromstring(chart_data.replace(b'local-data', b'Sheet1') if placement == 'inline' else chart_data)
        if placement == 'inline':
            rows = chart.findall('.//table:table-row', NS)
            for row, value in zip(rows[1:4], ('-1.25', '0', '')):
                cell = row.findall('table:table-cell', NS)[1]
                cell.attrib.pop(f"{{{NS['office']}}}value", None)
                if value:
                    cell.set(f"{{{NS['office']}}}value", value)
                cell.find('text:p', NS).text = value
            rows[1].find('table:table-cell/text:p', NS).text = 'جنوری & January'
        if placement == 'unsupported':
            chart.find('.//chart:chart', NS).set(f"{{{NS['chart']}}}class", 'chart:unsupported')
        frame = (f'<draw:frame text:anchor-type="{"as-char" if placement == "inline" else "paragraph"}" '
                 'svg:width="240pt" svg:height="180pt" svg:x="36pt" svg:y="54pt">'
                 '<draw:object xlink:href="./Object%201"/></draw:frame>')
        body = '<text:p>Chart test</text:p>' if placement == 'header' else (
            f'<text:p><text:span>{frame}</text:span></text:p>' if placement == 'inline' else frame)
        source_styles = styles
        if placement == 'header':
            style_root = ET.fromstring(styles)
            master = style_root.find('.//style:master-page', NS)
            header = ET.SubElement(master, f"{{{NS['style']}}}header")
            paragraph = ET.SubElement(header, f"{{{NS['text']}}}p")
            paragraph.append(ET.fromstring(f'<wrapper {namespaces}>{frame}</wrapper>')[0])
            source_styles = ET.tostring(style_root, encoding='utf-8')
        with ZipFile(source, 'w') as package:
            package.writestr('mimetype', 'application/vnd.oasis.opendocument.text')
            package.writestr('content.xml', f'<office:document-content {namespaces}><office:automatic-styles/>'
                              f'<office:body><office:text>{body}</office:text></office:body></office:document-content>')
            package.writestr('meta.xml', f'<office:document-meta {namespaces}><office:meta/></office:document-meta>')
            package.writestr('styles.xml', source_styles)
            package.writestr('Object 1/content.xml', ET.tostring(chart, encoding='utf-8'))
            package.writestr('META-INF/manifest.xml', f'<manifest:manifest xmlns:manifest="{ODF}manifest:1.0">'
                              '<manifest:file-entry manifest:full-path="/" manifest:media-type="application/vnd.oasis.opendocument.text"/>'
                              '<manifest:file-entry manifest:full-path="content.xml" manifest:media-type="text/xml"/>'
                              '<manifest:file-entry manifest:full-path="styles.xml" manifest:media-type="text/xml"/>'
                              '<manifest:file-entry manifest:full-path="Object 1/" manifest:media-type="application/vnd.oasis.opendocument.chart"/>'
                              '<manifest:file-entry manifest:full-path="Object 1/content.xml" manifest:media-type="text/xml"/>'
                              '</manifest:manifest>')
        if placement == 'unsupported':
            convert(source, target, succeeds=False)
            assert not target.exists(), 'unsupported chart produced a silently incomplete DOCX'
        else:
            convert(source, target)
            with ZipFile(target) as package:
                part = 'word/header1.xml' if placement == 'header' else 'word/document.xml'
                root = ET.fromstring(package.read(part))
                assert len(root.findall(f'.//{C}chart')) == 1, placement
                rels = ET.fromstring(package.read(posixpath.dirname(part) + '/_rels/' + posixpath.basename(part) + '.rels'))
                relation = root.find(f'.//{C}chart').get(R + 'id')
                assert any(x.get('Id') == relation and x.get('Type').endswith('/chart') for x in rels)
                assert not root.findall(f'.//{W}r/{W}r'), 'nested drawing run'
            if placement == 'inline':
                roundtrip = source.with_name('inline-reopened.odt')
                convert(target, roundtrip)
                with ZipFile(roundtrip) as package:
                    assert odf_series(package.read('Chart1/content.xml'))[0] == ['-1.25', '0', '']
                verify_docx(target, 1)

        print(f'PASS: {placement}')
