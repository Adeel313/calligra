#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Compare native PPTX text/background options with LibreOffice on common ODP."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import zipfile
from lxml import etree as E
from pptx import Presentation
from pptx.dml.color import RGBColor
from pptx.util import Inches, Pt
from pptx.enum.shapes import MSO_SHAPE

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
A = 'http://schemas.openxmlformats.org/drawingml/2006/main'
P = 'http://schemas.openxmlformats.org/presentationml/2006/main'
NS = {'a': A, 'p': P}


def environment():
    install = ROOT/'destdir/usr'
    return dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_QPA_PLATFORMTHEME='generic',
                QT_LOGGING_RULES='*.debug=false', GSETTINGS_BACKEND='memory', SAL_USE_VCLPLUGIN='svp',
                LD_LIBRARY_PATH=str(ROOT/'build/bin')+':'+str(install/'lib/x86_64-linux-gnu'),
                QT_PLUGIN_PATH=str(install/'lib/x86_64-linux-gnu/qt6/plugins'),
                XDG_DATA_DIRS=str(install/'share')+':/usr/local/share:/usr/share')


def fixture(path):
    deck = Presentation()
    slide = deck.slides.add_slide(deck.slide_layouts[6])
    slide.background.fill.solid()
    slide.background.fill.fore_color.rgb = RGBColor(20, 35, 65)
    box = slide.shapes.add_textbox(Inches(.5), Inches(.5), Inches(8), Inches(2))
    box.name = 'Formatting'
    frame = box.text_frame
    frame.margin_left, frame.margin_top = Pt(24), Pt(18)
    frame.margin_right, frame.margin_bottom = Pt(12), Pt(6)
    paragraph = frame.paragraphs[0]
    paragraph.space_before, paragraph.space_after = Pt(9), Pt(7)
    paragraph.line_spacing = 1.35
    paragraph._p.get_or_add_pPr().set('marR', '152400')
    for text, attrs in [('OPTION_TEXT ', {}), ('SUPERSCRIPT', {'baseline':'30000'}),
                        (' SUBSCRIPT', {'baseline':'-25000'}), (' CAPS', {'cap':'all'}),
                        (' SPACED', {'spc':'200'})]:
        run = paragraph.add_run();run.text = text;run.font.size = Pt(20)
        run.font.color.rgb = RGBColor(255, 255, 255)
        for key, value in attrs.items():run._r.get_or_add_rPr().set(key, value)
    box = slide.shapes.add_textbox(Inches(.5), Inches(3), Inches(8), Inches(2))
    box.name = 'Numbered'
    for index in range(2):
        paragraph = box.text_frame.paragraphs[0] if index == 0 else box.text_frame.add_paragraph()
        paragraph.text = 'NUMBER_'+str(index+3)
        paragraph.font.size = Pt(24);paragraph.font.color.rgb = RGBColor(255,255,255)
        props = paragraph._p.get_or_add_pPr();props.set('marL','457200');props.set('indent','-228600')
        E.SubElement(props, '{'+A+'}buAutoNum', type='arabicParenR', startAt='3')
    mask = slide.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(.5), Inches(6), Inches(8), Inches(.6))
    mask.name = 'Axial opacity'
    mask.fill.gradient()
    gradient = mask._element.spPr.find('a:gradFill', NS)
    stops = gradient.find('a:gsLst', NS)
    for child in list(stops):stops.remove(child)
    for position, alpha in [(0,0),(25000,15000),(50000,30000),(75000,15000),(100000,0)]:
        stop = E.SubElement(stops,'{'+A+'}gs',pos=str(position))
        color = E.SubElement(stop,'{'+A+'}srgbClr',val='000000')
        E.SubElement(color,'{'+A+'}alpha',val=str(alpha))
    gradient.find('a:lin',NS).set('ang','16200000')
    deck.save(path)


def settings(path):
    with zipfile.ZipFile(path) as package:
        root = E.fromstring(package.read('ppt/slides/slide1.xml'))
    shapes = {}
    for shape in root.findall('.//p:sp', NS):
        text = ''.join(shape.xpath('.//a:t/text()', namespaces=NS))
        if 'OPTION_TEXT' in text:shapes['text'] = shape
        if 'NUMBER_3' in text:shapes['list'] = shape
    result = {}
    background = root.find('p:cSld/p:bg/p:bgPr/a:solidFill/a:srgbClr',NS)
    result['background'] = background.get('val','').upper() if background is not None else None
    text = shapes['text']
    body = text.find('p:txBody/a:bodyPr',NS)
    for key in ['lIns','tIns','rIns','bIns']: result[key] = body.get(key)
    props = text.find('p:txBody/a:p/a:pPr',NS)
    for name, child in [('line_spacing','a:lnSpc/a:spcPct'),('space_before','a:spcBef/a:spcPts'),('space_after','a:spcAft/a:spcPts')]:
        node = props.find(child,NS) if props is not None else None
        result[name] = int(node.get('val')) if node is not None else None
    for run in text.findall('.//a:r',NS):
        value = ''.join(run.xpath('./a:t/text()', namespaces=NS));fmt = run.find('a:rPr',NS)
        for token, attribute in [('SUPERSCRIPT','baseline'),('SUBSCRIPT','baseline'),('CAPS','cap'),('SPACED','spc')]:
            if token in value:result[token] = fmt.get(attribute)
    props = shapes['list'].find('p:txBody/a:p/a:pPr',NS)
    result['list_margin'] = props.get('marL')
    result['list_indent'] = props.get('indent')
    number = props.find('a:buAutoNum',NS)
    result['number_type'] = number.get('type') if number is not None else None
    result['number_start'] = number.get('startAt') if number is not None else None
    gradients = root.xpath('.//p:sp[p:nvSpPr/p:cNvPr/@name="Axial opacity"]//a:gsLst', namespaces=NS)
    result['axial_opacity'] = [(int(stop.get('pos')),int(stop.find('a:srgbClr/a:alpha',NS).get('val'))
                              if stop.find('a:srgbClr/a:alpha',NS) is not None else 100000)
                             for stop in gradients[0]] if gradients else None
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args = parser.parse_args();out = args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    source = out/'source.pptx';fixture(source)
    commands = [ ['/usr/bin/python3',HERE/'libreoffice_reference.py',source,out/'common.odp'],
                 [ROOT/'build/bin/calligraconverter','--batch',out/'common.odp',out/'native.pptx'],
                 ['/usr/bin/python3',HERE/'libreoffice_reference.py',out/'common.odp',out/'reference.pptx'],
                 ['/usr/bin/python3',HERE/'libreoffice_reference.py',out/'reference.pptx',out/'reference.pdf'],
                 ['/usr/bin/python3',HERE/'libreoffice_reference.py',out/'native.pptx',out/'native.pdf'] ]
    for index, command in enumerate(commands):
        with (out/f'conversion-{index}.log').open('wb') as log:
            subprocess.run(command,env=environment(),stdout=log,stderr=log,check=True,timeout=180)
    native, reference = settings(out/'native.pptx'), settings(out/'reference.pptx')
    # ODF lengths can differ by one EMU after independent floating-point
    # conversion (one EMU is 1/12700 point). Do not confuse that with loss.
    lengths = {'lIns', 'tIns', 'rIns', 'bIns', 'list_margin', 'list_indent'}
    checks = {key: (native.get(key) is not None and abs(int(native[key])-int(value)) <= 2)
              if key in lengths else native.get(key)==value for key,value in reference.items()}
    checks['axial_opacity'] = (native.get('axial_opacity') is not None
        and len(native['axial_opacity'])==len(reference['axial_opacity'])
        and all(a[0]==b[0] and abs(a[1]-b[1])<=2 for a,b in zip(native['axial_opacity'],reference['axial_opacity'])))
    # Missing settings in the reference must not turn a feature test into a no-op.
    assert all(value is not None for value in reference.values()), reference
    import fitz
    from PIL import Image, ImageChops, ImageStat
    expected_pdf, actual_pdf = fitz.open(out/'reference.pdf'), fitz.open(out/'native.pdf')
    expected_pix = expected_pdf[0].get_pixmap(alpha=False)
    actual_pix = actual_pdf[0].get_pixmap(alpha=False)
    expected = Image.frombytes('RGB',(expected_pix.width,expected_pix.height),expected_pix.samples)
    actual = Image.frombytes('RGB',(actual_pix.width,actual_pix.height),actual_pix.samples).resize(expected.size)
    difference = ImageChops.difference(expected,actual)
    similarity = 1-sum(ImageStat.Stat(difference).mean)/(3*255)
    expected.save(out/'reference.png');actual.save(out/'native.png');difference.save(out/'difference.png')
    checks['rendering'] = len(actual_pdf)==len(expected_pdf)==1 and similarity >= .98
    report = dict(passed=all(checks.values()),checks=checks,native=native,reference=reference,pixel_similarity=similarity)
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':raise SystemExit(main())
