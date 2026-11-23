#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Check table-column formulas and cached dashboard charts with the native filter."""
import argparse
import os
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET
import zipfile

from openpyxl import Workbook
from openpyxl.chart import BarChart, PieChart, Reference
from openpyxl.chart.series import SeriesLabel
from openpyxl.chart.data_source import NumData, NumVal, StrData, StrVal, StrRef
from openpyxl.worksheet.table import Table

ROOT = Path(__file__).resolve().parents[4]
NS = {'t': 'urn:oasis:names:tc:opendocument:xmlns:table:1.0',
      'c': 'urn:oasis:names:tc:opendocument:xmlns:chart:1.0',
      'o': 'urn:oasis:names:tc:opendocument:xmlns:office:1.0',
      'text': 'urn:oasis:names:tc:opendocument:xmlns:text:1.0'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    book = Workbook()
    dashboard = book.active
    dashboard.title = 'Dashboard'
    data = book.create_sheet("Sales ' data")
    for row in [('Region', 'Revenue'), ('North', 10), ('South', 20), ('North', 30), ('Total', 60)]:
        data.append(row)
    data.add_table(Table(displayName='SalesTable', ref='A1:B5', totalsRowCount=1))
    dashboard.append(['North', '=SUMIF(SalesTable[Region],A1,SalesTable[Revenue])'])
    dashboard.append(['South', '=SUMIF(salestable[Region],A2,salestable[Revenue])'])
    dashboard['C1'] = '="SalesTable[Revenue]"'
    for kind in (BarChart, PieChart):
        chart = kind()
        chart.add_data(Reference(dashboard, min_col=2, min_row=1, max_row=2))
        chart.set_categories(Reference(dashboard, min_col=1, min_row=1, max_row=2))
        series = chart.series[0]
        series.tx = SeriesLabel(v='Revenue')
        series.val.numRef.f = 'Dashboard!B1:B2'
        series.val.numRef.numCache = NumData(ptCount=2, pt=[NumVal(idx=0, v=40), NumVal(idx=1, v=20)])
        series.cat.numRef = None
        series.cat.strRef = StrRef(f='Dashboard!A1:A2', strCache=StrData(ptCount=2, pt=[StrVal(idx=0, v='North'), StrVal(idx=1, v='South')]))
        dashboard.add_chart(chart, 'E1' if kind is BarChart else 'E17')
    source, target = out / 'dashboard.xlsx', out / 'dashboard.ods'
    book.save(source)
    install = ROOT / 'build-opt/local-install'
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_LOGGING_RULES='*.debug=false',
               LD_LIBRARY_PATH=str(ROOT / 'build-opt/bin'),
               QT_PLUGIN_PATH=str(install / 'lib/x86_64-linux-gnu/qt6/plugins'),
               XDG_DATA_DIRS=str(install / 'share') + ':/usr/share')
    with (out / 'convert.log').open('w') as log:
        subprocess.run([ROOT / 'build-opt/bin/calligraconverter', '--batch', source, target], env=env, stdout=log, stderr=log, check=True, timeout=60)
    with zipfile.ZipFile(target) as package:
        content = ET.fromstring(package.read('content.xml'))
        sheet = content.find('.//t:table[@t:name="Dashboard"]', NS)
        formulas = [c.get('{' + NS['t'] + '}formula') for c in sheet.findall('.//t:table-cell', NS)]
        formulas = [f for f in formulas if f]
        assert formulas == ["=SUMIF('Sales '' data'!$A$2:$A$4;A1;'Sales '' data'!$B$2:$B$4)",
                            '="SalesTable[Revenue]"',
                            "=SUMIF('Sales '' data'!$A$2:$A$4;A2;'Sales '' data'!$B$2:$B$4)"], formulas
        charts = [name for name in package.namelist() if name.startswith('Chart') and name.endswith('/content.xml')]
        assert len(charts) == 2, charts
        for name in charts:
            chart = ET.fromstring(package.read(name))
            assert chart.find('.//c:title/text:p', NS).text == 'Revenue'
            cells = chart.findall('.//t:table-cell', NS)
            values = [float(c.get('{' + NS['o'] + '}value')) for c in cells if c.get('{' + NS['o'] + '}value') is not None]
            assert values == [40, 20], (name, values)
            texts = [p.text for p in chart.findall('.//t:table//text:p', NS)]
            assert all(label in texts for label in ('North', 'South', 'Revenue')), texts
    print('PASS: forward table references, header/totals exclusion, quoted sheet names, string literals, chart caches and inline titles')


if __name__ == '__main__':
    main()
