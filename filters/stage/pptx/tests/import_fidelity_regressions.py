#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Regress transparent PPTX gradients and embedded styles overriding preset IDs."""
import argparse
import hashlib
import subprocess
import zipfile
from pathlib import Path
from lxml import etree as E
from pptx import Presentation
from pptx.enum.shapes import MSO_SHAPE
from pptx.util import Inches
from export_options_regressions import environment

ROOT = Path(__file__).resolve().parents[4]
A = 'http://schemas.openxmlformats.org/drawingml/2006/main'
DRAW = 'urn:oasis:names:tc:opendocument:xmlns:drawing:1.0'
SVG = 'urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0'
STYLE = 'urn:oasis:names:tc:opendocument:xmlns:style:1.0'
FO = 'urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0'
ID = '{69C7853C-536D-4A76-A0AE-DD22124D55A5}'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    out = parser.parse_args().output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    source = out / 'import.pptx'
    deck = Presentation()
    slide = deck.slides.add_slide(deck.slide_layouts[6])
    shape = slide.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(1), Inches(1), Inches(4), Inches(2))
    props = shape._element.find('{'+ 'http://schemas.openxmlformats.org/presentationml/2006/main' +'}spPr')
    props.append(E.fromstring(f'''<a:gradFill xmlns:a="{A}"><a:gsLst>
        <a:gs pos="100000"><a:srgbClr val="FFFFFF"/></a:gs>
        <a:gs pos="0"><a:srgbClr val="FFFFFF"><a:alpha val="0"/></a:srgbClr></a:gs>
        <a:gs pos="35000"><a:srgbClr val="FFFFFF"><a:alpha val="79000"/></a:srgbClr></a:gs>
        </a:gsLst><a:lin ang="10800000"/></a:gradFill>'''))
    slide = deck.slides.add_slide(deck.slide_layouts[6])
    table = slide.shapes.add_table(2, 2, Inches(1), Inches(1), Inches(4), Inches(2)).table
    table.cell(0, 0).text = 'CUSTOM_STYLE'
    table._tbl.find('{'+A+'}tblPr/{'+A+'}tableStyleId').text = ID
    slide = deck.slides.add_slide(deck.slide_layouts[6])
    themed = slide.shapes.add_table(2, 2, Inches(1), Inches(1), Inches(4), Inches(2)).table
    themed._tbl.find('{'+A+'}tblPr/{'+A+'}tableStyleId').text = '{11111111-1111-1111-1111-111111111111}'
    slide = deck.slides.add_slide(deck.slide_layouts[6])
    group = slide.shapes.add_group_shape()
    child = group.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(1), Inches(1), Inches(2), Inches(1))
    props = child._element.find('{http://schemas.openxmlformats.org/presentationml/2006/main}spPr')
    props.append(E.fromstring(f'<a:solidFill xmlns:a="{A}"><a:schemeClr val="accent3"><a:tint val="40000"/></a:schemeClr></a:solidFill>'))
    group_id = group.shape_id
    slide._element.append(E.fromstring(f'''<p:timing xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main">
      <p:tnLst><p:par><p:cTn id="1" dur="indefinite" nodeType="tmRoot"><p:childTnLst>
      <p:seq><p:cTn id="2" dur="indefinite" nodeType="mainSeq"><p:childTnLst><p:par>
      <p:cTn id="3" presetID="1" presetClass="entr" nodeType="clickEffect"><p:childTnLst>
      <p:set><p:cBhvr><p:cTn id="4" dur="1"/><p:tgtEl><p:spTgt spid="{group_id}"/></p:tgtEl>
      <p:attrNameLst><p:attrName>style.visibility</p:attrName></p:attrNameLst></p:cBhvr>
      <p:to><p:strVal val="visible"/></p:to></p:set></p:childTnLst></p:cTn>
      </p:par></p:childTnLst></p:cTn></p:seq></p:childTnLst></p:cTn></p:par></p:tnLst></p:timing>'''))
    deck.save(source)
    with zipfile.ZipFile(source) as package:
        parts = {name: package.read(name) for name in package.namelist()}
    parts['ppt/tableStyles.xml'] = f'''<a:tblStyleLst xmlns:a="{A}" def="{ID}">
        <a:tblStyle styleId="{ID}" styleName="Customized preset">
        <a:wholeTbl><a:tcStyle><a:fill><a:solidFill><a:srgbClr val="A024C8"/>
        </a:solidFill></a:fill></a:tcStyle></a:wholeTbl></a:tblStyle>
        <a:tblStyle styleId="{{11111111-1111-1111-1111-111111111111}}" styleName="Theme gradient">
        <a:tblBg><a:fillRef idx="2"><a:schemeClr val="accent3"/></a:fillRef></a:tblBg>
        <a:wholeTbl><a:tcStyle><a:fill><a:noFill/></a:fill></a:tcStyle></a:wholeTbl>
        </a:tblStyle></a:tblStyleLst>'''.encode()
    theme = E.fromstring(parts['ppt/theme/theme1.xml'])
    theme.find('.//{'+A+'}accent3/{'+A+'}srgbClr').set('val', '196B24')
    fills = theme.find('.//{'+A+'}fillStyleLst')
    fills[1] = E.fromstring(f'''<a:gradFill xmlns:a="{A}"><a:gsLst>
        <a:gs pos="0"><a:schemeClr val="phClr"/></a:gs>
        <a:gs pos="50000"><a:schemeClr val="phClr"><a:lumMod val="105000"/>
        <a:satMod val="103000"/><a:tint val="73000"/></a:schemeClr></a:gs>
        <a:gs pos="100000"><a:schemeClr val="phClr"/></a:gs>
        </a:gsLst><a:lin ang="5400000"/></a:gradFill>''')
    parts['ppt/theme/theme1.xml'] = E.tostring(theme)
    with zipfile.ZipFile(source, 'w', zipfile.ZIP_DEFLATED) as package:
        for name, data in parts.items(): package.writestr(name, data)
    before = hashlib.sha256(source.read_bytes()).hexdigest()
    target = out / 'import.odp'
    with (out / 'conversion.log').open('w') as log:
        subprocess.run([ROOT/'build/bin/calligraconverter', '--batch', source, target],
                       env=environment(), stdout=log, stderr=log, check=True, timeout=120)
    with zipfile.ZipFile(target) as package:
        roots = [E.fromstring(package.read(name)) for name in ['styles.xml', 'content.xml']]
    opacity = [node for root in roots for node in root.iter('{'+DRAW+'}opacity')]
    ramp = next(node for node in opacity if node.get('{'+DRAW+'}start') == '0%')
    assert ramp.get('{'+DRAW+'}angle') == '270deg'
    stops = [(float(n.get('{'+SVG+'}offset')), float(n.get('{'+SVG+'}stop-opacity')))
             for n in ramp]
    assert len(stops) == 3 and stops[0] == (0, 0) and stops[-1] == (1, 1), stops
    assert abs(stops[1][1] - .79) < .001, stops
    assert any(node.get('{'+DRAW+'}opacity-name') == ramp.get('{'+DRAW+'}name')
               for root in roots for node in root.iter())
    assert any(node.get('{'+FO+'}background-color') == '#a024c8'
               for root in roots for node in root.iter('{'+STYLE+'}table-cell-properties')), 'Embedded style replaced by preset'
    assert any(node.get('{'+FO+'}background-color') == '#91a892'
               for root in roots for node in root.iter('{'+STYLE+'}table-cell-properties')), 'Theme gradient transforms lost'
    assert any(node.get('{'+DRAW+'}fill-color') == '#ccd4cd'
               for root in roots for node in root.iter()), 'Tint applied in display RGB'
    group_target = f'slide3item{group_id}'
    assert any(node.get('{'+DRAW+'}id') == group_target
               for root in roots for node in root.iter('{'+DRAW+'}g')), 'Group animation identity lost'
    assert any(node.get('{urn:oasis:names:tc:opendocument:xmlns:smil-compatible:1.0}targetElement') == group_target
               for root in roots for node in root.iter()), 'Group animation target lost'
    assert hashlib.sha256(source.read_bytes()).hexdigest() == before
    print('PASS: sorted transparent gradient, opacity direction, embedded preset override, source unchanged')


if __name__ == '__main__':
    main()
