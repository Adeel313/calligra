#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Verify cached SmartArt drawings retain the slide frame during conversion."""
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile
from lxml import etree as E
from pptx import Presentation
from pptx.util import Inches

P = 'http://schemas.openxmlformats.org/presentationml/2006/main'
A = 'http://schemas.openxmlformats.org/drawingml/2006/main'
R = 'http://schemas.openxmlformats.org/officeDocument/2006/relationships'
REL = 'http://schemas.openxmlformats.org/package/2006/relationships'
D = 'http://schemas.microsoft.com/office/drawing/2008/diagram'
DGM = 'http://schemas.openxmlformats.org/drawingml/2006/diagram'
NS = {'p': P, 'a': A}


def fixture(path):
    prs = Presentation()
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    box = slide.shapes.add_textbox(Inches(.5), Inches(.3), Inches(8), Inches(.5))
    box.text = 'SmartArt placement regression'
    prs.save(path)
    with zipfile.ZipFile(path) as z:
        parts = {n: z.read(n) for n in z.namelist()}
    slide = E.fromstring(parts['ppt/slides/slide1.xml'])
    tree = slide.find('p:cSld/p:spTree', NS)
    tree.append(E.fromstring(f'''<p:graphicFrame xmlns:p="{P}" xmlns:a="{A}" xmlns:r="{R}">
      <p:nvGraphicFramePr><p:cNvPr id="3" name="Diagram"/><p:cNvGraphicFramePr/><p:nvPr/></p:nvGraphicFramePr>
      <p:xfrm><a:off x="1828800" y="1828800"/><a:ext cx="3657600" cy="1828800"/></p:xfrm>
      <a:graphic><a:graphicData uri="{DGM}"><dgm:relIds xmlns:dgm="{DGM}" r:dm="rIdData" r:lo="rIdLayout" r:qs="rIdStyle" r:cs="rIdColors"/></a:graphicData></a:graphic>
    </p:graphicFrame>'''))
    paragraph = slide.find('.//a:p', NS)
    paragraph.append(E.fromstring(f'<a:r xmlns:a="{A}"><a:t xml:space="preserve"> </a:t></a:r>'))
    paragraph.append(E.fromstring(f'<a:r xmlns:a="{A}"><a:t>SpaceSentinel</a:t></a:r>'))
    paragraph.append(E.fromstring(f'<mc:AlternateContent xmlns:mc="http://schemas.openxmlformats.org/markup-compatibility/2006" xmlns:a14="http://schemas.microsoft.com/office/drawing/2010/main" xmlns:m="http://schemas.openxmlformats.org/officeDocument/2006/math"><mc:Choice Requires="a14"><a14:m><m:oMath><m:r><m:t>MathSentinel</m:t></m:r><m:sSup><m:e><m:r><m:t>x</m:t></m:r></m:e><m:sup><m:r><m:t>2</m:t></m:r></m:sup></m:sSup></m:oMath></a14:m></mc:Choice><mc:Fallback/></mc:AlternateContent>'))
    parts['ppt/slides/slide1.xml'] = E.tostring(slide)
    rels = E.fromstring(parts['ppt/slides/_rels/slide1.xml.rels'])
    E.SubElement(rels, f'{{{REL}}}Relationship', Id='rIdData', Type=R+'/diagramData', Target='../diagrams/data1.xml')
    E.SubElement(rels, f'{{{REL}}}Relationship', Id='rIdDrawing', Type='http://schemas.microsoft.com/office/2007/relationships/diagramDrawing', Target='../diagrams/drawing1.xml')
    parts['ppt/slides/_rels/slide1.xml.rels'] = E.tostring(rels)
    parts['ppt/diagrams/data1.xml'] = f'<dgm:dataModel xmlns:dgm="{DGM}" xmlns:a="{A}"><dgm:extLst><a:ext uri="{D}"><dsp:dataModelExt xmlns:dsp="{D}" relId="rIdDrawing"/></a:ext></dgm:extLst></dgm:dataModel>'.encode()
    parts['ppt/diagrams/drawing1.xml'] = f'''<dsp:drawing xmlns:dsp="{D}" xmlns:a="{A}"><dsp:spTree>
      <dsp:nvGrpSpPr><dsp:cNvPr id="1" name="Cached diagram"/><dsp:cNvGrpSpPr/></dsp:nvGrpSpPr>
      <dsp:grpSpPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="3657600" cy="1828800"/><a:chOff x="0" y="0"/><a:chExt cx="3657600" cy="1828800"/></a:xfrm></dsp:grpSpPr>
      <dsp:sp modelId="node1"><dsp:nvSpPr><dsp:cNvPr id="2" name="Diagram label"/><dsp:cNvSpPr/></dsp:nvSpPr>
        <dsp:spPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="3657600" cy="1828800"/></a:xfrm><a:prstGeom prst="rect"><a:avLst/></a:prstGeom><a:solidFill><a:srgbClr val="156082"/></a:solidFill></dsp:spPr>
        <dsp:txBody><a:bodyPr/><a:lstStyle/><a:p><a:r><a:rPr sz="2400"/><a:t>Cached SmartArt sentinel</a:t></a:r></a:p></dsp:txBody>
      </dsp:sp>
    </dsp:spTree></dsp:drawing>'''.encode()
    types = E.fromstring(parts['[Content_Types].xml'])
    for part, typ in [('data1.xml', 'diagramData'), ('drawing1.xml', 'diagramDrawing')]:
        content = ('application/vnd.ms-office.drawingml.diagramDrawing+xml' if typ == 'diagramDrawing' else 'application/vnd.openxmlformats-officedocument.drawingml.diagramData+xml')
        E.SubElement(types, '{http://schemas.openxmlformats.org/package/2006/content-types}Override', PartName='/ppt/diagrams/'+part, ContentType=content)
    parts['[Content_Types].xml'] = E.tostring(types)
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as z:
        for n, data in parts.items(): z.writestr(n, data)


def main():
    repo = Path(__file__).resolve().parents[4]
    with tempfile.TemporaryDirectory(prefix='stage-smartart-test-') as temp:
        source, dest = Path(temp)/'source.pptx', Path(temp)/'output.odp'
        fixture(source)
        before = hashlib.sha256(source.read_bytes()).digest()
        subprocess.run([repo/'build/bin/calligraofficeconverter', '--input', source, '--output', dest,
                        '--from', 'pptx', '--to', 'odp'], check=True, timeout=90)
        assert hashlib.sha256(source.read_bytes()).digest() == before, 'Original PPTX modified'
        ns = {'draw': 'urn:oasis:names:tc:opendocument:xmlns:drawing:1.0',
              'svg': 'urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0'}
        with zipfile.ZipFile(dest) as z: root = E.fromstring(z.read('content.xml'))
        assert 'regression SpaceSentinel' in ''.join(root.itertext()), 'Standalone space between text runs disappeared'
        assert 'MathSentinel' in ''.join(root.itertext()), 'Office Math with empty fallback disappeared'
        shapes = [s for s in root.findall('.//draw:custom-shape', ns) if 'Cached SmartArt sentinel' in ''.join(s.itertext())]
        assert len(shapes) == 1, 'Cached diagram label missing or duplicated'
        def cm(value):
            if value.endswith('cm'): return float(value[:-2])
            if value.endswith('in'): return float(value[:-2])*2.54
            if value.endswith('pt'): return float(value[:-2])*2.54/72
            raise AssertionError(value)
        shape = shapes[0]
        for attr, expected in [('x', 5.08), ('y', 5.08), ('width', 10.16), ('height', 5.08)]:
            value = cm(shape.get(f'{{{ns["svg"]}}}{attr}'))
            assert abs(value-expected)<.02, (attr, value, expected)
        print('PASS: cached SmartArt text, position, dimensions, and unchanged source')

if __name__ == '__main__': main()
