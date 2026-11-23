#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Native XLSX color-scale, traffic-light and sparkline import/export regression."""
import argparse
import os
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET
import zipfile
from openpyxl import Workbook
from openpyxl.formatting.rule import ColorScaleRule, IconSetRule

ROOT = Path(__file__).resolve().parents[4]
M = 'http://schemas.openxmlformats.org/spreadsheetml/2006/main'
V = 'http://www.calligra.org/2002/sheets'
X14 = 'http://schemas.microsoft.com/office/spreadsheetml/2009/9/main'
XM = 'http://schemas.microsoft.com/office/excel/2006/main'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--source', type=Path)
    parser.add_argument('--roundtrip', action='store_true')
    args = parser.parse_args()
    out = args.output.resolve(); out.mkdir(parents=True, exist_ok=False)
    source = args.source.resolve() if args.source else out / 'visuals.xlsx'
    if not args.source:
        book = Workbook(); sheet = book.active; sheet.title = 'Dashboard'
        for row, value in enumerate([0, 25, 50, 75, 100], 1):
            sheet.cell(row, 1, value); sheet.cell(row, 3, value)
        sheet.conditional_formatting.add('A1:A5', ColorScaleRule(start_type='min', start_color='FFF8696B', mid_type='percentile', mid_value=50, mid_color='FFFFEB84', end_type='max', end_color='FF63BE7B'))
        sheet.conditional_formatting.add('C1:C5', IconSetRule('3TrafficLights1', 'percent', [0,33,67], showValue=True))
        book.save(source)
        with zipfile.ZipFile(source) as archive: parts = {p:archive.read(p) for p in archive.namelist()}
        # Excel stores sparklines in its x14 extension; openpyxl cannot write them.
        extension = f'''<extLst><ext uri="{{05C60535-1F16-4fd2-B633-F4F36F0B64E0}}"><x14:sparklineGroups xmlns:x14="{X14}" xmlns:xm="{XM}"><x14:sparklineGroup type="column" displayEmptyCellsAs="gap"><x14:colorSeries theme="4" tint="0.2"/><x14:sparklines><x14:sparkline><xm:f>Dashboard!A1:A5</xm:f><xm:sqref>B7</xm:sqref></x14:sparkline></x14:sparklines></x14:sparklineGroup></x14:sparklineGroups></ext></extLst>'''
        parts['xl/worksheets/sheet1.xml'] = parts['xl/worksheets/sheet1.xml'].replace(b'</worksheet>', extension.encode() + b'</worksheet>')
        with zipfile.ZipFile(source, 'w', zipfile.ZIP_DEFLATED) as archive:
            for p, data in parts.items(): archive.writestr(p, data)
    install = ROOT / 'build-opt/local-install'
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_QPA_PLATFORMTHEME='generic', QT_LOGGING_RULES='*.debug=false',
               LD_LIBRARY_PATH=str(ROOT/'build-opt/bin'), QT_PLUGIN_PATH=str(install/'lib/x86_64-linux-gnu/qt6/plugins'),
               XDG_DATA_DIRS=str(install/'share')+':/usr/share')
    def convert(src, target):
        with target.with_suffix('.log').open('w') as log:
            subprocess.run([ROOT/'build-opt/bin/calligraconverter', '--batch', src, target], env=env, stdout=log, stderr=log, check=True, timeout=180)
    ods = out / 'visuals.ods'; convert(source, ods)
    with zipfile.ZipFile(ods) as archive: root = ET.fromstring(archive.read('content.xml'))
    visuals = root.findall(f'.//{{{V}}}cell-visuals'); assert visuals, 'visual rules lost on import'
    rules = [r for v in visuals for r in v.findall(f'{{{V}}}rule')]
    assert {r.get('type') for r in rules} >= {'colorScale','iconSet'}
    sparks = [s for v in visuals for s in v.findall(f'{{{V}}}sparkline')]; assert sparks, 'sparkline lost'
    assert all(s.get('source') and s.get('target') for s in sparks)
    if args.roundtrip:
        saved = out / 'roundtrip.xlsx'; convert(ods, saved)
        with zipfile.ZipFile(saved) as archive:
            sheets = [ET.fromstring(archive.read(p)) for p in archive.namelist() if p.startswith('xl/worksheets/sheet') and p.endswith('.xml')]
        exported_rules = [r for s in sheets for r in s.findall(f'.//{{{M}}}cfRule') if r.get('type') in ('colorScale','iconSet')]
        exported_sparks = [r for s in sheets for r in s.findall(f'.//{{{X14}}}sparkline')]
        assert len(exported_rules)==len(rules) and len(exported_sparks)==len(sparks)
        assert sorted((r.get('type'),r.get('priority')) for r in exported_rules)==sorted((r.get('type'),r.get('priority')) for r in rules)
        imported = out / 'reimport.ods'; convert(saved, imported)
        with zipfile.ZipFile(imported) as archive: again=ET.fromstring(archive.read('content.xml'))
        assert len(again.findall(f'.//{{{V}}}rule'))==len(rules)
        assert len(again.findall(f'.//{{{V}}}sparkline'))==len(sparks)
    print(f'PASS: {len(rules)} color/icon rules and {len(sparks)} live sparklines' + (' survive native XLSX round trip' if args.roundtrip else ' imported'))


if __name__ == '__main__':
    main()
