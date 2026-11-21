#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Verify editable charts, their points and sheet references across XLSX export.

Uses generated single-series, single-point, multi-series and scatter charts.
Requires openpyxl and the locally built and installed native filters.
"""
import argparse
import importlib.util
import math
from pathlib import Path
import subprocess
import zipfile
import xml.etree.ElementTree as ET
from openpyxl import Workbook, load_workbook
from openpyxl.chart import BarChart, ScatterChart, Reference, Series
from openpyxl.chart.data_source import NumData, NumVal
from openpyxl.chart.series import DataPoint
from openpyxl.chart.label import DataLabelList, DataLabel
from openpyxl.worksheet.datavalidation import DataValidation
from openpyxl.formatting.rule import CellIsRule
from openpyxl.styles import PatternFill
from openpyxl.utils.cell import range_to_tuple

from package_content_types import check_package

ROOT = Path(__file__).resolve().parents[4]
C = '{http://schemas.openxmlformats.org/drawingml/2006/chart}'


def check_workbook(source, target):
    """Check real workbooks without relying on ZIP size or private fixtures."""
    before, after = load_workbook(source), load_workbook(target)
    assert before.sheetnames == after.sheetnames
    chart_count = 0
    for original, saved in zip(before, after):
        original_cells = {c.coordinate: c.value for row in original for c in row if c.value is not None}
        saved_cells = {c.coordinate: c.value for row in saved for c in row if c.value is not None}
        assert original_cells == saved_cells, f'Cell values or formulas changed in {original.title}'
        assert set(original.merged_cells.ranges) == set(saved.merged_cells.ranges)
        assert len(original._charts) == len(saved._charts), f'Charts lost in {original.title}'
        for old_chart, new_chart in zip(original._charts, saved._charts):
            chart_count += 1
            assert type(old_chart) is type(new_chart), 'Chart type changed'
            assert len(old_chart.series) == len(new_chart.series), 'Series lost'
            for old_series, new_series in zip(old_chart.series, new_chart.series):
                if old_series.tx is not None and old_series.tx.strRef is not None:
                    assert new_series.tx is not None and new_series.tx.strRef is not None, 'Series label reference lost'
                    assert range_to_tuple(old_series.tx.strRef.f) == range_to_tuple(new_series.tx.strRef.f)
                for field in ('cat', 'val', 'xVal', 'yVal'):
                    old_data, new_data = getattr(old_series, field), getattr(new_series, field)
                    if old_data is None:
                        continue
                    assert new_data is not None, f'{field} lost'
                    for kind in ('strRef', 'numRef'):
                        old_ref = getattr(old_data, kind, None)
                        if old_ref is None:
                            continue
                        new_ref = getattr(new_data, kind, None)
                        assert new_ref is not None, f'{field} reference lost'
                        # Quoting a sheet name is legal even when not required.
                        assert range_to_tuple(old_ref.f) == range_to_tuple(new_ref.f)
                        cache = 'numCache' if kind == 'numRef' else 'strCache'
                        old_cache, new_cache = getattr(old_ref, cache), getattr(new_ref, cache)
                        if old_cache is None:
                            continue
                        assert new_cache is not None, f'{field} cache lost'
                        old_points = {p.idx: p.v for p in old_cache.pt}
                        new_points = {p.idx: p.v for p in new_cache.pt}
                        assert old_points.keys() == new_points.keys(), f'{field} points lost'
                        for index, value in old_points.items():
                            actual = new_points[index]
                            assert (math.isclose(float(value), float(actual), rel_tol=1e-14, abs_tol=1e-14)
                                    if kind == 'numRef' else value == actual), (field, index, value, actual)
    assert chart_count, 'Source workbook has no charts to test'
    print(f'PASS: {chart_count} editable charts, references, cached points, cells, formulas and merges retained')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--source', type=Path, help='Round-trip an existing workbook instead of generated charts')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    source, ods, target = out / 'original.xlsx', out / 'intermediate.ods', out / 'exported.xlsx'
    if args.source:
        source = args.source.resolve()
        spec = importlib.util.spec_from_file_location('options', ROOT / 'filters/words/docx/export/tests/export_options.py')
        options = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(options)
        for name, src, dst in [('import', source, ods), ('export', ods, target)]:
            with (out / (name + '.log')).open('w') as log:
                subprocess.run([ROOT / 'build/bin/calligraconverter', '--batch', src, dst],
                               env=options.environment(), stdout=log, stderr=log, check=True, timeout=120)
        check_package(target)
        check_workbook(source, target)
        return
    workbook = Workbook()
    sheet = workbook.active
    sheet.title = "Chart Data"
    for row in [('Category', 'First', 'Second'), ('Alpha', 12, 4), ('Beta', 7, 9), ('Gamma', 3, 6)]:
        sheet.append(row)
    sheet['A2'].hyperlink = 'https://example.org/'
    sheet.merge_cells('D1:E1')
    sheet.auto_filter.ref = 'A1:C4'
    rule = CellIsRule(operator='greaterThan', formula=['5'], fill=PatternFill('solid', fgColor='FFFF00'))
    sheet.conditional_formatting.add('B2:B4', rule)
    validation = DataValidation(type='whole', operator='between', formula1=0, formula2=100)
    sheet.add_data_validation(validation)
    validation.add('C2:C4')
    for count, end_row, anchor, title in [(1, 4, 'E2', 'Single series'), (1, 2, 'E18', 'Single point'),
                                         (2, 4, 'O2', 'Two series')]:
        chart = BarChart()
        chart.title = title
        chart.add_data(Reference(sheet, min_col=2, max_col=1+count, min_row=1, max_row=end_row), titles_from_data=True)
        chart.set_categories(Reference(sheet, min_col=1, min_row=2, max_row=end_row))
        if title in ('Single series', 'Two series'):
            chart.series[0].val.numRef.numCache = NumData(
                ptCount=3, pt=[NumVal(idx=i, v=v) for i, v in enumerate([12, 7, 3])])
        if title == 'Single series':
            chart.legend = None
            chart.varyColors = True
            chart.series[0].dLbls = DataLabelList(showVal=True)
            for i, color in enumerate(['00B050', 'C0504D']):
                point = DataPoint(idx=i)
                point.graphicalProperties.solidFill = color
                chart.series[0].data_points.append(point)
        if title == 'Two series':
            chart.series[0].dLbls = DataLabelList(showVal=False, dLbl=[DataLabel(idx=0, showVal=True)])
        sheet.add_chart(chart, anchor)
    scatter = ScatterChart()
    scatter.title = 'Scatter'
    scatter.series.append(Series(Reference(sheet, min_col=3, min_row=2, max_row=4),
                                 Reference(sheet, min_col=2, min_row=2, max_row=4), title='Pairs'))
    sheet.add_chart(scatter, 'O18')
    workbook.save(source)
    spec = importlib.util.spec_from_file_location('options', ROOT / 'filters/words/docx/export/tests/export_options.py')
    options = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(options)
    for name, src, dst in [('import', source, ods), ('export', ods, target)]:
        with (out / (name + '.log')).open('w') as log:
            subprocess.run([ROOT / 'build/bin/calligraconverter', '--batch', src, dst],
                           env=options.environment(), stdout=log, stderr=log, check=True, timeout=120)
    check_package(target)
    with zipfile.ZipFile(target) as package:
        names = [n for n in package.namelist() if n.startswith('xl/charts/') and n.endswith('.xml')]
        assert len(names) == 4, ('Charts were lost or replaced by pictures', names)
        charts = [ET.fromstring(package.read(n)) for n in sorted(names)]
    title_tag = '{http://schemas.openxmlformats.org/drawingml/2006/main}t'
    by_title = {''.join(e.text or '' for e in chart.findall('.//' + C + 'title//' + title_tag)): chart for chart in charts}
    expected = {'Single series': [12, 7, 3], 'Single point': [12],
                'Two series': [12, 7, 3], 'Scatter': [4, 9, 6]}
    assert set(by_title) == set(expected), by_title.keys()
    for title, values in expected.items():
        chart = by_title[title]
        series = chart.findall('.//' + C + 'ser')
        assert series
        value_tag = 'yVal' if chart.find('.//' + C + 'scatterChart') is not None else 'val'
        actual = [float(e.text) for e in series[0].findall(C + value_tag + '//' + C + 'pt/' + C + 'v')]
        assert actual == values, (actual, values)
        assert chart.find('.//' + C + 'title/' + C + 'overlay').attrib['val'] == '0'
        formulas = [e.text for e in series[0].findall('.//' + C + 'f')]
        assert formulas and all(f.startswith("'Chart Data'!") for f in formulas), formulas
    assert len(by_title['Two series'].findall('.//' + C + 'ser')) == 2
    original_charts = load_workbook(source).active._charts
    exported_charts = load_workbook(target).active._charts
    assert len(exported_charts) == 4
    original_cells = {(chart.anchor._from.col, chart.anchor._from.row) for chart in original_charts}
    exported_cells = set()
    for chart in exported_charts:
        anchor = chart.anchor
        assert hasattr(anchor, 'to'), 'Cell-anchored chart must retain a worksheet-relative rectangle'
        exported_cells.add((anchor._from.col, anchor._from.row))
        assert anchor.to.col > anchor._from.col and anchor.to.row > anchor._from.row
    assert exported_cells == original_cells
    single = by_title['Single series']
    assert single.find('.//' + C + 'legend') is None
    label = single.find('.//' + C + 'ser/' + C + 'tx/' + C + 'strRef/' + C + 'f')
    assert label is not None and label.text == "'Chart Data'!$B$1"
    assert single.find('.//' + C + 'roundedCorners').attrib['val'] == '0'
    assert single.find('.//' + C + 'dLbls/' + C + 'showVal').attrib['val'] == '1'
    rgb = '{http://schemas.openxmlformats.org/drawingml/2006/main}srgbClr'
    point_colors = [e.attrib['val'] for e in single.findall('.//' + C + 'dPt/' + C + 'spPr//' + rgb)]
    assert point_colors == ['00B050', 'C0504D', '9BBB59'], point_colors
    fill = single.find(C + 'spPr//' + rgb)
    assert fill is not None and fill.attrib['val'] == 'FFFFFF'
    for axis in single.findall('.//' + C + 'catAx') + single.findall('.//' + C + 'valAx'):
        assert axis.find(C + 'delete').attrib['val'] == '0'
        assert axis.find(C + 'tickLblPos').attrib['val'] == 'nextTo'
    overrides = by_title['Two series'].findall('.//' + C + 'dLbls/' + C + 'dLbl')
    assert any(e.find(C + 'idx').attrib['val'] == '0' and e.find(C + 'showVal').attrib['val'] == '1' for e in overrides)
    print(f'PASS: four charts retain editable series, points, titles and sheet references; artifacts: {out}')


if __name__ == '__main__':
    main()
