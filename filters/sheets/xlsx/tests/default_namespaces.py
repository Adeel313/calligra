#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Check that XLSX drawings and charts written with default namespaces import.

Excel and openpyxl bind the spreadsheetDrawing and chart namespaces as the
default namespace (``<wsDr xmlns="...">``), while LibreOffice uses the
conventional ``xdr:``/``c:`` prefixes. The import filter has to read both
spellings. This generates a workbook with openpyxl (default namespaces), converts
it with the native filter and checks that the drawing objects and values reach
the ODS output.

Run with /usr/bin/python3; requires openpyxl and Pillow. The output folder must
be new; the generated workbook, the converted ODS and the converter log are
retained there.
"""
import argparse
import importlib.util
import subprocess
import zipfile
from pathlib import Path

from openpyxl import Workbook
from openpyxl.chart import BarChart, Reference
from openpyxl.comments import Comment
from openpyxl.drawing.image import Image as XLImage
from PIL import Image as PILImage

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

    picture = out / 'dot.png'
    PILImage.new('RGB', (40, 30), (220, 30, 30)).save(picture)

    workbook = Workbook()
    sheet = workbook.active
    sheet.title = 'Data'
    sheet['A1'] = 'Quarterly Report'
    sheet.merge_cells('A1:C1')
    sheet['A2'], sheet['B2'] = 'Widget', 12
    sheet['A3'], sheet['B3'] = 'Gadget', 7
    sheet['A4'], sheet['B4'] = 'Thing', 3
    sheet['C2'] = '=SUM(B2:B4)'
    sheet['D2'] = '=C2*2'
    sheet['E2'].value = 45658
    sheet['E2'].number_format = 'yyyy-mm-dd'
    sheet['B3'].comment = Comment('hello from a note', 'tester')
    sheet.add_image(XLImage(str(picture)), 'G2')

    chart_sheet = workbook.create_sheet('Chart')
    for row in range(1, 4):
        chart_sheet.cell(row=row, column=1, value=row)
        chart_sheet.cell(row=row, column=2, value=row * 2)
    chart = BarChart()
    chart.add_data(Reference(chart_sheet, min_col=2, min_row=1, max_row=3))
    chart.set_categories(Reference(chart_sheet, min_col=1, min_row=1, max_row=3))
    chart_sheet.add_chart(chart, 'D2')

    source = out / 'default-namespaces.xlsx'
    workbook.save(source)

    with zipfile.ZipFile(source) as package:
        drawing = package.read('xl/drawings/drawing1.xml').decode()
        chart_xml = package.read('xl/charts/chart1.xml').decode()
    assert 'xmlns="http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing"' in drawing, drawing[:200]
    assert 'xmlns="http://schemas.openxmlformats.org/drawingml/2006/chart"' in chart_xml, chart_xml[:200]

    target = out / 'default-namespaces.ods'
    log = out / 'convert.log'
    with log.open('w') as handle:
        subprocess.run([ROOT / 'build/bin/calligraconverter', '--batch', source, target], env=env, stdout=handle, stderr=handle, check=True, timeout=180)

    log_text = log.read_text()
    assert 'Element "' not in log_text, log_text

    with zipfile.ZipFile(target) as package:
        names = package.namelist()
        content = package.read('content.xml').decode()
    assert any(name.startswith('Chart1/') for name in names), names
    assert 'draw:image' in content, content[:400]
    # openpyxl stores the note in xl/comments/comment1.xml, not xl/comments1.xml.
    assert '<office:annotation>' in content, content[:400]
    assert 'hello from a note' in content, content[:400]
    assert '<dc:creator>tester</dc:creator>' in content, content[:400]
    assert 'table:number-columns-spanned="3"' in content, content[:400]
    assert 'table:formula="=SUM(B2:B4)"' in content, content[:400]
    assert 'office:value="12"' in content, content[:400]
    assert 'number:date-style' in content, content[:400]
    print(f'PASS: default-namespace drawing and chart parts import; artifacts: {out}')


if __name__ == '__main__':
    main()
