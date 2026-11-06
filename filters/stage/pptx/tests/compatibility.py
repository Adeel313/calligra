#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Exercise Stage's real document model against LibreOffice (not just ZIP conversion)."""
import argparse
import copy
import posixpath
import concurrent.futures
import io
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import unicodedata
import zipfile
from lxml import etree as ET
from PIL import Image, ImageChops, ImageStat
import fitz
from pptx import Presentation
from pptx.chart.data import CategoryChartData, XyChartData
from pptx.dml.color import RGBColor
from pptx.enum.chart import XL_CHART_TYPE
from pptx.enum.shapes import MSO_SHAPE, MSO_CONNECTOR
from pptx.enum.text import PP_ALIGN
from pptx.util import Inches, Pt

HERE = Path(__file__).resolve().parent
NS = {'p':'http://schemas.openxmlformats.org/presentationml/2006/main',
      'a':'http://schemas.openxmlformats.org/drawingml/2006/main',
      'c':'http://schemas.openxmlformats.org/drawingml/2006/chart',
      'draw':'urn:oasis:names:tc:opendocument:xmlns:drawing:1.0',
      'office':'urn:oasis:names:tc:opendocument:xmlns:office:1.0',
      'anim':'urn:oasis:names:tc:opendocument:xmlns:animation:1.0',
      'smil':'urn:oasis:names:tc:opendocument:xmlns:smil-compatible:1.0',
      'presentation':'urn:oasis:names:tc:opendocument:xmlns:presentation:1.0'}
PPTX = 'application/vnd.openxmlformats-officedocument.presentationml.presentation'

def run(command, env=None, timeout=90, log=None):
    completed = subprocess.run([str(x) for x in command], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
    if log: Path(log).write_bytes(completed.stdout)
    if completed.returncode:
        raise RuntimeError(f'exit {completed.returncode}: {" ".join(map(str,command))}\n{completed.stdout.decode(errors="replace")[-1800:]}')
    return completed

def normalize(text):
    return ''.join(c for c in unicodedata.normalize('NFC',text) if not c.isspace() and c not in '\u200e\u200f\u202a\u202b\u202c\u2066\u2067\u2068\u2069')

def expand_saved_diagrams(package, part, root):
    """Include authored cached SmartArt in semantic checks, as in rendering."""
    relpart=posixpath.join(posixpath.dirname(part),'_rels',posixpath.basename(part)+'.rels')
    if relpart not in package.namelist():return
    rels={rel.get('Id'):rel for rel in ET.fromstring(package.read(relpart))}
    def resolve(target):return posixpath.normpath(posixpath.join(posixpath.dirname(part),target)).lstrip('/')
    for frame in root.xpath('//*[local-name()="graphicFrame"]'):
        ids=frame.xpath('.//*[local-name()="relIds"]')
        if not ids:continue
        data_rel=rels.get(ids[0].get('{'+NS['r']+'}dm')) if 'r' in NS else rels.get(ids[0].get('{http://schemas.openxmlformats.org/officeDocument/2006/relationships}dm'))
        if data_rel is None:continue
        data=ET.fromstring(package.read(resolve(data_rel.get('Target'))))
        ext=data.xpath('//*[local-name()="dataModelExt"]')
        if not ext:continue
        drawing_rel=rels.get(ext[0].get('relId'))
        if drawing_rel is None or not drawing_rel.get('Type','').endswith('/diagramDrawing'):continue
        drawing=ET.fromstring(package.read(resolve(drawing_rel.get('Target'))))
        trees=drawing.xpath('//*[local-name()="spTree"]')
        if trees:frame.append(copy.deepcopy(trees[0]))


def canonical_math_text(root):
    math_ns = 'http://schemas.openxmlformats.org/officeDocument/2006/math'
    def value(node):
        tag = ET.QName(node).localname
        if tag.endswith('Pr'): return ''
        if tag == 't': return node.text or ''
        def child(name):
            item = node.find('{'+math_ns+'}'+name)
            return value(item) if item is not None else ''
        if tag == 'd':
            props = node.find('{'+math_ns+'}dPr')
            def delimiter(name, default):
                item = props.find('{'+math_ns+'}'+name) if props is not None else None
                return item.get('{'+math_ns+'}val', default) if item is not None else default
            return delimiter('begChr','(')+''.join(value(e) for e in node.findall('{'+math_ns+'}e'))+delimiter('endChr',')')
        if tag == 'nary':
            item = node.find('{'+math_ns+'}naryPr/{'+math_ns+'}chr')
            return (item.get('{'+math_ns+'}val','∑') if item is not None else '∑')+child('sub')+child('sup')+child('e')
        if tag == 'groupChr':
            symbol = node.find('{'+math_ns+'}groupChrPr/{'+math_ns+'}chr')
            return child('e')+(symbol.get('{'+math_ns+'}val','⏟') if symbol is not None else '⏟')
        if tag == 'acc':
            accent = node.find('{'+math_ns+'}accPr/{'+math_ns+'}chr')
            return child('e')+(accent.get('{'+math_ns+'}val','\u0302') if accent is not None else '\u0302')
        if tag == 'f': return '('+child('num')+')/('+child('den')+')'
        return ''.join(value(c) for c in node)
    for expression in root.xpath('//*[local-name()="m" and namespace-uri()="http://schemas.microsoft.com/office/drawing/2010/main"]'):
        run = ET.Element('{'+NS['a']+'}r')
        ET.SubElement(run,'{'+NS['a']+'}t').text = value(expression)
        expression.getparent().replace(expression,run)


def select_alternate_content(root):
    """Count one authored representation, rather than choice plus fallback."""
    for alternate in reversed(root.xpath('//*[local-name()="AlternateContent"]')):
        choices = alternate.xpath('./*[local-name()="Choice"]')
        fallback = alternate.xpath('./*[local-name()="Fallback"]')
        selected = next((choice for choice in choices if choice.get('Requires') != 'v'), None)
        if selected is None:
            selected = fallback[0] if fallback else (choices[0] if choices else None)
        parent = alternate.getparent()
        if parent is None: continue
        position = parent.index(alternate)
        if selected is not None:
            for child in list(selected):
                parent.insert(position, child)
                position += 1
        parent.remove(alternate)


def features(path):
    result = dict(line_breaks=0, text_highlights=[], transition_settings=[], table_fills=[], bar_directions=[], table_cells=[], slides=0, tables=0, charts=0, pictures=0, groups=0, connectors=0, transitions=0, timing=0, media=0, hidden=0, hidden_indices=[], slide_texts={}, media_payloads=[], links=[], text='', notes='', chart_values=[], slide_size=[])
    with zipfile.ZipFile(path) as z:
        presentation = ET.fromstring(z.read('ppt/presentation.xml'))
        # Section metadata also contains sldId entries; only the main slide list
        # defines presentation pages.
        result['slides'] = len(presentation.xpath('./p:sldIdLst/p:sldId', namespaces=NS))
        size = presentation.xpath('//*[local-name()="sldSz"]')
        if size: result['slide_size'] = [int(size[0].get('cx')),int(size[0].get('cy'))]
        for name in sorted(z.namelist()):
            if name.startswith('ppt/media/') and Path(name).suffix.lower() in ['.mp4','.mp3','.wav','.webm','.avi','.m4a']:
                result['media_payloads'].append(hashlib.sha256(z.read(name)).hexdigest())
            if not name.endswith(('.xml','.rels')): continue
            root = ET.fromstring(z.read(name))
            if re.fullmatch(r'ppt/slides/slide\d+\.xml',name):
                select_alternate_content(root)
                canonical_math_text(root)
                expand_saved_diagrams(z,name,root)
                canonical_math_text(root)
                for key,tag in [('tables','tbl'),('pictures','pic'),('groups','grpSp'),('connectors','cxnSp'),('timing','timing')]:
                    result[key] += len(root.xpath(f'//*[local-name()="{tag}"]'))
                # LibreOffice emits empty transition defaults, sometimes twice
                # inside AlternateContent. Count slides with an actual effect
                # or automatic advance, not implementation-specific XML nodes.
                transitions = root.xpath('//*[local-name()="transition"]')
                result['transitions'] += any(len(t) or t.get('advTm') is not None
                                             or t.get('advClick') == '0' for t in transitions)
                for transition in transitions:
                    if len(transition) or transition.get('advTm') is not None or transition.get('advClick') == '0':
                        effects=[ET.QName(child).localname for child in transition if ET.QName(child).localname != 'extLst']
                        result['transition_settings'].append([int(re.search(r'slide(\d+)\.xml',name).group(1)), transition.get('advTm'), transition.get('advClick','1'), effects])
                result['line_breaks'] += len(root.findall('.//a:br',NS)) + sum(t.count('\n') for t in root.xpath('.//a:t/text()',namespaces=NS))
                for run in root.findall('.//a:r',NS):
                    color=run.find('a:rPr/a:highlight/a:srgbClr',NS)
                    if color is not None:result['text_highlights'].append([normalize(''.join(run.xpath('.//a:t/text()',namespaces=NS))),color.get('val','').upper()])
                for table_index, table in enumerate(root.xpath('//*[local-name()="tbl"]')):
                    cells = []
                    for row_index, row in enumerate(table.findall('a:tr', NS)):
                        for col_index, cell in enumerate(row.findall('a:tc', NS)):
                            if cell.get('hMerge') in ('1','true') or cell.get('vMerge') in ('1','true'):
                                continue
                            fill = cell.find('a:tcPr/a:solidFill/a:srgbClr', NS)
                            if fill is not None:
                                result['table_fills'].append([len(result['table_cells']), row_index, col_index, fill.get('val','').upper()])
                            cells.append([row_index, col_index, int(cell.get('rowSpan','1')),
                                          int(cell.get('gridSpan','1')),
                                          normalize(''.join(cell.xpath('.//*[local-name()="t"]/text()')))])
                    result['table_cells'].append(cells)
                result['hidden'] += root.get('show')=='0'
                index=int(re.search(r'slide(\d+)\.xml',name).group(1))
                if root.get('show')=='0':result['hidden_indices'].append(index)
                result['slide_texts'][index]=''.join(root.xpath('//*[local-name()="t"]/text()'))
                result['media'] += len(root.xpath('//*[local-name()="videoFile" or local-name()="audioFile"]'))
                result['text'] += ''.join(root.xpath('//*[local-name()="t"]/text()'))
            if re.fullmatch(r'ppt/notesSlides/notesSlide\d+\.xml',name):
                for shape in root.findall('.//p:sp', NS):
                    placeholder=shape.find('p:nvSpPr/p:nvPr/p:ph', NS)
                    if placeholder is not None and placeholder.get('type') in ('sldImg','sldNum','hdr','ftr','dt'):continue
                    result['notes'] += ''.join(shape.xpath('.//*[local-name()="t"]/text()'))
            if re.fullmatch(r'ppt/charts/chart\d+\.xml',name):
                result['charts'] += 1
                result['chart_values'] += root.xpath('//*[local-name()="v"]/text()')
                result['bar_directions'] += root.xpath('//*[local-name()="barDir"]/@val')
            if name.endswith('.rels'):
                for rel in root:
                    if rel.get('Type','').endswith('/hyperlink') and rel.get('TargetMode')=='External': result['links'].append(rel.get('Target'))
    return result

def rewrite(path, transform):
    with zipfile.ZipFile(path) as source:
        entries = [(item, source.read(item.filename)) for item in source.infolist()]
    with zipfile.ZipFile(path,'w') as target:
        for item,data in entries: target.writestr(item,transform(item.filename,data))

def new_deck(width=13.333333,height=7.5):
    p=Presentation();p.slide_width=Inches(width);p.slide_height=Inches(height);return p

def text(slide,value,x=.5,y=.5,w=10,h=1,size=24):
    box=slide.shapes.add_textbox(Inches(x),Inches(y),Inches(w),Inches(h))
    box.text=value
    for paragraph in box.text_frame.paragraphs:
        for run_ in paragraph.runs: run_.font.name='Noto Sans';run_.font.size=Pt(size)
    return box

def fixtures(root,worker,env):
    root.mkdir(parents=True,exist_ok=True)
    images=root/'assets';images.mkdir(exist_ok=True)
    im=Image.new('RGBA',(200,120),(0,0,0,0))
    for x in range(200):
        for y in range(120): im.putpixel((x,y),(x,60,200,180 if x<100 else 255))
    im.save(images/'alpha.png');im.convert('RGB').save(images/'photo.jpg')
    cases=[]
    def save(name,p,tokens=(),notes=(),kind='core'):
        path=root/(name+'.pptx');p.save(path);cases.append(dict(name=name,path=str(path),tokens=list(tokens),notes=list(notes),kind=kind));return path
    p=new_deck();p.slides.add_slide(p.slide_layouts[6]);save('blank',p)
    p=new_deck();s=p.slides.add_slide(p.slide_layouts[6]);box=text(s,'EDIT_BEFORE')
    save('edit_round_trip',p,['EDIT_BEFORE STAGE_EDIT_SENTINEL'],kind='edit')
    box.text_frame.paragraphs[0].runs[0].text+=' STAGE_EDIT_SENTINEL'
    expected=root/'edit_expected.pptx';p.save(expected);cases[-1]['expected_path']=str(expected)
    p=new_deck()
    for i,layout in enumerate([0,1,5]):
        s=p.slides.add_slide(p.slide_layouts[layout]);s.shapes.title.text=f'CASE_LAYOUT_{i}'
        text(s,f'CASE_BODY_{i}',y=3)
    save('placeholders_order',p,[f'CASE_LAYOUT_{i}' for i in range(3)]+[f'CASE_BODY_{i}' for i in range(3)])
    p=new_deck();s=p.slides.add_slide(p.slide_layouts[6]);box=text(s,'CASE_TEXT',w=11,h=5)
    tokens=['CASE_TEXT','BOLD_RUN','ITALIC_RUN','UNDERLINED_RUN','FONT_COLOR_RUN']
    for index,value in enumerate(tokens[1:]):
        paragraph=box.text_frame.add_paragraph();r=paragraph.add_run();r.text=value;r.font.name='Noto Sans';r.font.size=Pt(16+index*5)
        r.font.bold=index==0;r.font.italic=index==1;r.font.underline=index==2;r.font.color.rgb=RGBColor(180,30,70)
        paragraph.alignment=[PP_ALIGN.LEFT,PP_ALIGN.CENTER,PP_ALIGN.RIGHT,PP_ALIGN.JUSTIFY][index]
    save('text_runs_alignment',p,tokens)
    p=new_deck();s=p.slides.add_slide(p.slide_layouts[6]);values=['CASE_UNICODE','اردو عربی متن','日本語のプレゼンテーション','中文简体文字','café naïve résumé','Ελληνικά Кириллица']
    for i,value in enumerate(values): text(s,value,y=.3+i, h=.8,size=22)
    save('unicode_rtl',p,values)
    p=new_deck();s=p.slides.add_slide(p.slide_layouts[6]);box=text(s,'CASE_LISTS',h=6)
    for i in range(5):
        par=box.text_frame.add_paragraph();par.text=f'LIST_ITEM_{i}';par.level=i%3
        prop=par._p.get_or_add_pPr();bullet=ET.SubElement(prop,'{'+NS['a']+'}buChar');bullet.set('char','•')
    save('bullets_indentation',p,['CASE_LISTS']+[f'LIST_ITEM_{i}' for i in range(5)])
    p=new_deck();s=p.slides.add_slide(p.slide_layouts[6]);tokens=[]
    for i,shape_type in enumerate([MSO_SHAPE.RECTANGLE,MSO_SHAPE.OVAL,MSO_SHAPE.ROUNDED_RECTANGLE,MSO_SHAPE.STAR_5_POINT,MSO_SHAPE.RIGHT_ARROW,MSO_SHAPE.ISOSCELES_TRIANGLE]):
        sh=s.shapes.add_shape(shape_type,Inches(.5+(i%3)*4),Inches(.5+(i//3)*3),Inches(3),Inches(2));sh.fill.solid();sh.fill.fore_color.rgb=RGBColor(30+i*25,80,160);sh.line.color.rgb=RGBColor(200,45,20);sh.line.width=Pt(2);sh.rotation=i*5
        sh.text=f'SHAPE_{i}';tokens.append(sh.text)
    save('shapes_fill_lines_rotation',p,tokens)
    p=new_deck();s=p.slides.add_slide(p.slide_layouts[6]);text(s,'CASE_IMAGES')
    pic=s.shapes.add_picture(str(images/'alpha.png'),Inches(.8),Inches(2),width=Inches(4));pic.crop_left=.1;pic.crop_right=.2
    pic=s.shapes.add_picture(str(images/'photo.jpg'),Inches(6),Inches(2),width=Inches(4));pic.rotation=15
    save('pictures_alpha_crop_rotation',p,['CASE_IMAGES'])
    p=new_deck();s=p.slides.add_slide(p.slide_layouts[6]);text(s,'CASE_BITMAP_FILL')
    shape=s.shapes.add_shape(MSO_SHAPE.RECTANGLE,Inches(1),Inches(2),Inches(10),Inches(4))
    picture=s.shapes.add_picture(str(images/'photo.jpg'),Inches(0),Inches(0))
    fill=ET.Element('{'+NS['a']+'}blipFill')
    for child in picture._element.blipFill:fill.append(copy.deepcopy(child))
    for child in list(shape._element.spPr):
        if ET.QName(child).localname in ('solidFill','noFill','gradFill'):shape._element.spPr.remove(child)
    shape._element.spPr.append(fill)
    picture._element.getparent().remove(picture._element)
    save('bitmap_filled_shapes',p,['CASE_BITMAP_FILL'])
    p=new_deck();s=p.slides.add_slide(p.slide_layouts[6]);text(s,'CASE_GRADIENT_ALPHA')
    shape=s.shapes.add_shape(MSO_SHAPE.RECTANGLE,Inches(1),Inches(2),Inches(10),Inches(4))
    for child in list(shape._element.spPr):
        if ET.QName(child).localname in ('solidFill','noFill','gradFill'):shape._element.spPr.remove(child)
    fill=ET.SubElement(shape._element.spPr,'{'+NS['a']+'}gradFill')
    stops=ET.SubElement(fill,'{'+NS['a']+'}gsLst')
    for position,opacity in [(58000,None),(33000,64000),(0,0),(100000,None)]:
        stop=ET.SubElement(stops,'{'+NS['a']+'}gs',pos=str(position))
        color=ET.SubElement(stop,'{'+NS['a']+'}srgbClr',val='000000')
        if opacity is not None:ET.SubElement(color,'{'+NS['a']+'}alpha',val=str(opacity))
    ET.SubElement(fill,'{'+NS['a']+'}lin',ang='10800000',scaled='0')
    save('transparent_gradient_stops',p,['CASE_GRADIENT_ALPHA'])
    p=new_deck();s=p.slides.add_slide(p.slide_layouts[6]);text(s,'CASE_LITERAL_LINES')
    box=text(s,'',y=2,h=3);line_run=box.text_frame.paragraphs[0].add_run();line_run.text='LINE_A\nLINE_B\nLINE_C'
    save('literal_text_newlines',p,['CASE_LITERAL_LINES','LINE_A','LINE_B','LINE_C'])
    p=new_deck();s=p.slides.add_slide(p.slide_layouts[6]);text(s,'CASE_RUN_HIGHLIGHT')
    box=text(s,'',y=2,h=2);paragraph=box.text_frame.paragraphs[0]
    paragraph.add_run().text='prefix '
    highlight_run=paragraph.add_run();highlight_run.text='HIGHLIGHT_ME'
    highlight=ET.SubElement(highlight_run._r.get_or_add_rPr(),'{'+NS['a']+'}highlight');ET.SubElement(highlight,'{'+NS['a']+'}srgbClr',val='FFFF00')
    paragraph.add_run().text=' suffix'
    end=ET.SubElement(paragraph._p,'{'+NS['a']+'}endParaRPr')
    highlight=ET.SubElement(end,'{'+NS['a']+'}highlight');ET.SubElement(highlight,'{'+NS['a']+'}srgbClr',val='FF0000')
    save('run_highlight_scope',p,['CASE_RUN_HIGHLIGHT','HIGHLIGHT_ME'])
    cases[-1]['expected_highlights']=[['HIGHLIGHT_ME','FFFF00']]
    p=new_deck();s=p.slides.add_slide(p.slide_layouts[6]);g=s.shapes.add_group_shape();tokens=[]
    for i in range(2):
        sh=g.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(1+i*4), Inches(2), Inches(3), Inches(2));sh.text=f'GROUP_{i}';tokens.append(sh.text)
    s.shapes.add_connector(MSO_CONNECTOR.STRAIGHT,Inches(4),Inches(3),Inches(5),Inches(3))
    save('groups_connectors',p,tokens)
    p=new_deck();s=p.slides.add_slide(p.slide_layouts[6]);text(s,'CASE_TABLE')
    table=s.shapes.add_table(4,3,Inches(.7),Inches(2),Inches(11),Inches(3)).table;tokens=['CASE_TABLE']
    for r in range(4):
        for c in range(3):
            cell=table.cell(r,c);cell.text=f'CELL_{r}_{c}';tokens.append(cell.text)
            cell.fill.solid();cell.fill.fore_color.rgb=RGBColor(240-r*20,245-c*10,250)
    save('tables',p,tokens)
    p=new_deck();s=p.slides.add_slide(p.slide_layouts[6]);table=s.shapes.add_table(3,3,Inches(.7),Inches(1),Inches(11),Inches(4)).table
    table.cell(0,0).merge(table.cell(0,2));table.cell(0,0).text='MERGED_HEADER'
    table.cell(1,0).text='MERGED_BODY';save('tables_merged',p,['MERGED_HEADER','MERGED_BODY'])
    p=new_deck();s=p.slides.add_slide(p.slide_layouts[6]);table=s.shapes.add_table(1,1,Inches(1),Inches(1),Inches(5),Inches(2)).table
    table.cell(0,0).text='SINGLE_CELL_TABLE';save('table_single_cell',p,['SINGLE_CELL_TABLE'])
    for name,kind in [('column',XL_CHART_TYPE.COLUMN_CLUSTERED),('line',XL_CHART_TYPE.LINE),('pie',XL_CHART_TYPE.PIE),('bar',XL_CHART_TYPE.BAR_CLUSTERED)]:
        p=new_deck();s=p.slides.add_slide(p.slide_layouts[6]);text(s,'CHART_'+name)
        data=CategoryChartData();data.categories=['North','South','West'];data.add_series('Revenue',[12,23,34])
        if name!='pie': data.add_series('Costs',[5,8,13])
        chart=s.shapes.add_chart(kind,Inches(1),Inches(1.5),Inches(10),Inches(5),data).chart;chart.has_legend=True
        save('chart_'+name,p,['CHART_'+name])
    p=new_deck();s=p.slides.add_slide(p.slide_layouts[6]);data=XyChartData();series=data.add_series('Measurements')
    for x,y in [(1,2),(2,5),(3,7)]:series.add_data_point(x,y)
    s.shapes.add_chart(XL_CHART_TYPE.XY_SCATTER,Inches(1),Inches(1),Inches(10),Inches(5),data);save('chart_scatter',p)
    p=new_deck();s=p.slides.add_slide(p.slide_layouts[6]);box=text(s,'CASE_LINK');box.text_frame.paragraphs[0].runs[0].hyperlink.address='https://example.org/presentation?q=1&lang=en'
    s.notes_slide.notes_text_frame.text='NOTE_SENTINEL café اردو'
    save('notes_hyperlinks',p,['CASE_LINK'],['NOTE_SENTINEL café اردو'])
    for name,w,h in [('standard_4_3',10,7.5),('portrait',7.5,10),('custom_size',12,5)]:
        p=new_deck(w,h);s=p.slides.add_slide(p.slide_layouts[6]);text(s,'SIZE_'+name,w=w-1);save(name,p,['SIZE_'+name])
    p=new_deck()
    for i in range(3):text(p.slides.add_slide(p.slide_layouts[6]),f'TRANSITION_{i}')
    path=save('transitions_automatic',p,[f'TRANSITION_{i}' for i in range(3)])
    def add_transition(name,data):
        if re.fullmatch(r'ppt/slides/slide\d+\.xml',name):
            root_=ET.fromstring(data);tr=ET.Element('{'+NS['p']+'}transition',spd='med',advTm='2500',advClick='1');ET.SubElement(tr,'{'+NS['p']+'}fade');root_.append(tr);return ET.tostring(root_,xml_declaration=True,encoding='UTF-8',standalone=True)
        return data
    rewrite(path,add_transition)
    p=new_deck()
    for i in range(3):text(p.slides.add_slide(p.slide_layouts[6]),f'VISIBILITY_{i}')
    path=save('hidden_slide',p,[f'VISIBILITY_{i}' for i in range(3)])
    def hide(name,data):
        if name=='ppt/slides/slide2.xml':
            r=ET.fromstring(data);r.set('show','0');return ET.tostring(r,xml_declaration=True,encoding='UTF-8',standalone=True)
        return data
    rewrite(path,hide)
    p=new_deck()
    for i in range(30):text(p.slides.add_slide(p.slide_layouts[6]),f'LARGE_DECK_{i}')
    save('large_30_slides',p,['LARGE_DECK_0','LARGE_DECK_29'],kind='large')
    p=new_deck();s=p.slides.add_slide(p.slide_layouts[6]);text(s,'UNICODE_PATH');save('space name café اردو',p,['UNICODE_PATH'])
    # A real ODF animation exported by the reference engine yields a valid
    # PPTX timing tree without depending on private python-pptx timing APIs.
    seed=next(c for c in cases if c['name']=='text_runs_alignment')
    animated_odp=root/'animation-source.odp'
    run([worker,'--input',seed['path'],'--output',animated_odp,'--from','pptx','--to','odp'],env=env)
    def animate(name,data):
        if name!='content.xml':return data
        r=ET.fromstring(data);page=r.find('.//draw:page',NS);frame=page.find('.//draw:frame',NS)
        if frame is None:raise RuntimeError('No frame for animation fixture')
        ident='animation_target';frame.set('{http://www.w3.org/XML/1998/namespace}id',ident);frame.set('{'+NS['draw']+'}id',ident)
        def node(parent,tag,attrs):
            e=ET.SubElement(parent,'{'+NS['anim']+'}'+tag)
            for key,val in attrs.items():prefix,local=key.split(':');e.set('{'+NS[prefix]+'}'+local,val)
            return e
        top=node(page,'par',{'presentation:node-type':'timing-root'})
        seq=node(top,'seq',{'presentation:node-type':'main-sequence'})
        first=node(seq,'par',{'smil:begin':'next'})
        second=node(first,'par',{'smil:begin':'0s'})
        effect=node(second,'par',{'smil:begin':'0s','smil:targetElement':ident,'presentation:preset-id':'ooo-entrance-appear','presentation:preset-class':'entrance','presentation:node-type':'on-click'})
        node(effect,'set',{'smil:attributeName':'visibility','smil:to':'visible','smil:dur':'0.001s','smil:fill':'hold','smil:targetElement':ident})
        return ET.tostring(r,xml_declaration=True,encoding='UTF-8')
    rewrite(animated_odp,animate)
    animated=root/'animation_appear.pptx';run([worker,'--input',animated_odp,'--output',animated,'--from','odp','--to','pptx'],env=env)
    cases.append(dict(name='animation_appear',path=str(animated),tokens=seed['tokens'],notes=[],kind='core'))
    if shutil.which('ffmpeg'):
        movie=images/'video.mp4';run(['ffmpeg','-loglevel','error','-f','lavfi','-i','color=c=blue:s=96x64:d=1','-pix_fmt','yuv420p','-y',movie])
        p=new_deck();s=p.slides.add_slide(p.slide_layouts[6]);text(s,'CASE_VIDEO');s.shapes.add_movie(str(movie),Inches(1),Inches(2),Inches(6),Inches(4),poster_frame_image=str(images/'photo.jpg'),mime_type='video/mp4');save('embedded_video',p,['CASE_VIDEO'],kind='media')
    # LibreOffice's ODP picture frames contain an auxiliary text paragraph.
    # Native export must preserve the image rather than selecting that text.
    picture_case = next(c for c in cases if c['name']=='pictures_alpha_crop_rotation')
    picture_odp = root/'pictures_libreoffice.odp'
    run(['/usr/bin/python3',HERE/'libreoffice_reference.py',picture_case['path'],picture_odp],env=env)
    def ensure_image_text(name, data):
        if name!='content.xml':return data
        tree=ET.fromstring(data)
        for image in tree.xpath('//*[local-name()="image"]'):
            if not image.findall('{urn:oasis:names:tc:opendocument:xmlns:text:1.0}p'):
                ET.SubElement(image,'{urn:oasis:names:tc:opendocument:xmlns:text:1.0}p')
        return ET.tostring(tree,xml_declaration=True,encoding='UTF-8')
    rewrite(picture_odp,ensure_image_text)
    cases.append(dict(name='pictures_libreoffice_odp',path=str(picture_odp),
        expected_path=picture_case['path'],feature_source=picture_case['path'],
        tokens=picture_case['tokens'],notes=[],kind='core'))
    (root/'fixtures.json').write_text(json.dumps(cases,ensure_ascii=False,indent=2))
    return cases

def reference(input_path,directory,env):
    run(['/usr/bin/python3',HERE/'libreoffice_reference.py',input_path,directory/'reference.pdf',directory/'reference.pptx',directory/'reference.odp'],env=env,log=directory/'reference.log')

def check_case(case,output,harness,env):
    directory=output/case['name'];directory.mkdir(parents=True,exist_ok=True)
    result=dict(name=case['name'],kind=case['kind'],passed=False,checks={},issues=[])
    try:
        source=Path(case['path']);reference(Path(case.get('expected_path',source)),directory,env)
        with (directory/'stage.log').open('wb') as log:
            command=[str(harness),str(source),str(directory)]+(['--edit'] if case['kind']=='edit' else [])
            completed=subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=180)
        stage=json.loads((directory/'stage-result.json').read_text())
        result['stage']=stage
        if completed.returncode or not stage.get('pptx_saved'):
            raise RuntimeError(f'Stage document round trip failed (exit {completed.returncode}): {stage.get("error","")}')
        baseline=features(directory/'reference.pptx');actual=features(directory/'stage.pptx');original=features(Path(case.get('feature_source',source)))
        result['source_features']=original;result['reference_features']=baseline;result['export_features']=actual
        checks=result['checks'];checks['import_registered']=stage['import_registered'];checks['export_registered']=stage['export_registered'];checks['slide_count']=actual['slides']==baseline['slides']==stage['slides']
        checks['slide_size']=len(actual['slide_size'])==2 and all(abs(x-y)<=12700 for x,y in zip(actual['slide_size'],baseline['slide_size']))
        combined=normalize(actual['text']);checks['text']=all(normalize(t) in combined for t in case['tokens'])
        checks['notes']=all(normalize(t) in normalize(actual['notes']) for t in case['notes'])
        checks['hyperlinks']=set(baseline['links'])<=set(actual['links'])
        if case['kind']=='edit':
            checks['edit_retained']='STAGE_EDIT_SENTINEL' in actual['text']
        checks['slide_order']=all(normalize(t)==normalize(actual['slide_texts'].get(i,'').replace('STAGE_EDIT_SENTINEL','') if case['kind']=='edit' else actual['slide_texts'].get(i,'')) for i,t in original['slide_texts'].items())
        checks['media_payloads']=set(baseline['media_payloads'])<=set(actual['media_payloads'])
        for key in ['tables','charts','pictures','groups','connectors','transitions','timing','media','hidden']:
            checks[key]=actual[key]>=baseline[key]
        checks['transition_settings']=actual['transition_settings']==original['transition_settings']
        checks['line_breaks']=actual['line_breaks']>=original['line_breaks']
        if 'expected_highlights' in case:checks['text_highlights']=actual['text_highlights']==case['expected_highlights']
        checks['bar_direction']=actual['bar_directions']==baseline['bar_directions']
        checks['table_fills']=all(fill in actual['table_fills'] for fill in baseline['table_fills'])
        checks['table_cells']=actual['table_cells']==original['table_cells']
        checks['chart_data']=set(baseline['chart_values'])<=set(actual['chart_values'])
        scores=[]
        pdf=fitz.open(directory/'reference.pdf')
        visible=list(range(stage['slides']))
        for index,page in zip(visible,pdf):
            if (directory/f'stage-{index+1}.png').exists():
                native=Image.open(directory/f'stage-{index+1}.png').convert('RGBA');white=Image.new('RGBA',native.size,'white');white.alpha_composite(native);native=white.convert('RGB')
                pix=page.get_pixmap(matrix=fitz.Matrix(native.width/page.rect.width,native.width/page.rect.width),alpha=False)
                expected=Image.frombytes('RGB',(pix.width,pix.height),pix.samples).resize(native.size)
                expected.save(directory/f'reference-{index+1}.png')
                score=1-sum(ImageStat.Stat(ImageChops.difference(native,expected)).mean)/(3*255)
                scores.append(round(score,5))
        result['native_render_similarity']=scores;checks['native_render']=len(scores)==len(visible) and all(s>=.92 for s in scores)
        # Validate the exported deck with the independent application too.
        run(['/usr/bin/python3',HERE/'libreoffice_reference.py',directory/'stage.pptx',directory/'reopened.pdf'],env=env,log=directory/'reopen.log')
        checks['libreoffice_reopen']=len(fitz.open(directory/'reopened.pdf'))==actual['slides']
        result['issues']=[name for name,ok in checks.items() if not ok];result['passed']=not result['issues']
    except Exception as error:result['issues'].append(str(error))
    (directory/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2))
    print(('PASS ' if result['passed'] else 'FAIL ')+case['name']+': '+', '.join(result['issues']),flush=True)
    return result

def negatives(output,worker,env,valid_input,harness):
    def run_negative(*args, **kwargs):
        try:
            return subprocess.run(*args, **kwargs)
        except subprocess.TimeoutExpired as error:
            return subprocess.CompletedProcess(error.cmd,124,error.stdout,error.stderr)
    root=output/'negative';root.mkdir(exist_ok=True);tests=[]
    for name,data in [('not_zip',b'not a presentation'),('truncated_zip',Path(valid_input).read_bytes()[:100])]:
        path=root/(name+'.pptx');path.write_bytes(data);destination=root/(name+'.odp');destination.write_bytes(b'KEEP_EXISTING')
        proc=run_negative([str(worker),'--input',str(path),'--output',str(destination),'--from','pptx','--to','odp'],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=20)
        tests.append(dict(name=name,passed=proc.returncode!=0 and destination.read_bytes()==b'KEEP_EXISTING',exit=proc.returncode))
    malformed=root/'malformed_xml.pptx'
    shutil.copyfile(valid_input,malformed)
    rewrite(malformed,lambda name,data:b'<broken' if name=='ppt/presentation.xml' else data)
    destination=root/'malformed_xml.odp';destination.write_bytes(b'KEEP_EXISTING')
    proc=run_negative([str(worker),'--input',str(malformed),'--output',str(destination),'--from','pptx','--to','odp'],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=20)
    tests.append(dict(name='malformed_xml',passed=proc.returncode==3 and destination.read_bytes()==b'KEEP_EXISTING',exit=proc.returncode))
    destination=root/'no_engine.odp';destination.write_bytes(b'KEEP_EXISTING')
    proc=run_negative([str(worker),'--input',str(valid_input),'--output',str(destination),'--from','pptx','--to','odp','--engine','kit','--program-dir',str(root/'does-not-exist')],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=20)
    tests.append(dict(name='missing_libreoffice',passed=proc.returncode==78 and destination.read_bytes()==b'KEEP_EXISTING',exit=proc.returncode))
    fallback=root/'native_fallback';fallback.mkdir(exist_ok=True)
    fallback_env=env.copy();fallback_env['CALLIGRA_LIBREOFFICE_PROGRAM']=str(root/'does-not-exist')
    with (fallback/'stage.log').open('wb') as log:
        proc=run_negative([str(harness),str(valid_input),str(fallback)],env=fallback_env,stdout=log,stderr=subprocess.STDOUT,timeout=60)
    report=fallback/'stage-result.json'
    status=json.loads(report.read_text()) if report.exists() else {}
    tests.append(dict(name='native_import_without_backend',passed=proc.returncode==0 and status.get('opened') is True and status.get('pptx_saved') is True,exit=proc.returncode))
    export_error=root/'missing_backend_export';export_error.mkdir(exist_ok=True)
    with (export_error/'stage.log').open('wb') as log:
        proc=run_negative([str(harness),str(output/'fixtures/animation-source.odp'),str(export_error)],env=fallback_env,stdout=log,stderr=subprocess.STDOUT,timeout=60)
    report=export_error/'stage-result.json';status=json.loads(report.read_text()) if report.exists() else {}
    tests.append(dict(name='native_export_without_backend',passed=proc.returncode==0 and status.get('opened') and status.get('odp_saved') and status.get('pptx_saved') is True,exit=proc.returncode))
    destination=root/'missing_directory/output.odp'
    proc=run_negative([str(worker),'--input',str(valid_input),'--output',str(destination),'--from','pptx','--to','odp'],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=20)
    tests.append(dict(name='unwritable_destination',passed=proc.returncode!=0 and not destination.exists(),exit=proc.returncode))
    for item in tests:
        if item.get('exit')==124:
            item['passed']=False
            item['error']='subprocess timeout'
    for item in tests:print(('PASS ' if item['passed'] else 'FAIL ')+item['name'],flush=True)
    return tests

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--build',type=Path,required=True);parser.add_argument('--install',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);parser.add_argument('--jobs',type=int,default=2);parser.add_argument('--cases',nargs='*')
    parser.add_argument('--reuse-passing',action='store_true',help='Reuse passing presentation results and rerun failed presentations and all error checks')
    args=parser.parse_args();build=args.build.resolve();install=args.install.resolve();output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    env=os.environ.copy();env.update(QT_QPA_PLATFORM='offscreen',QT_QPA_PLATFORMTHEME='generic',GSETTINGS_BACKEND='memory',SAL_USE_VCLPLUGIN='svp',XDG_CONFIG_HOME=str(output/'config'),QT_PLUGIN_PATH=str(install/'lib/x86_64-linux-gnu/qt6/plugins'),LD_LIBRARY_PATH=str(install/'lib/x86_64-linux-gnu'),XDG_DATA_DIRS=str(install/'share')+':/usr/local/share:/usr/share',PATH=str(install/'bin')+':'+env.get('PATH',''))
    worker=build/'bin/calligraofficeconverter';harness=build/'bin/stage_pptx_roundtrip'
    manifest=output/'fixtures/fixtures.json'
    cases=json.loads(manifest.read_text()) if args.reuse_passing and manifest.exists() else fixtures(output/'fixtures',worker,env)
    if args.cases:cases=[c for c in cases if c['name'] in args.cases]
    def evaluate(case):
        cached=output/'cases'/case['name']/'result.json'
        if args.reuse_passing and cached.exists():
            result=json.loads(cached.read_text())
            if result.get('passed'):
                result['reused']=True
                print('REUSE PASS '+case['name'],flush=True)
                return result
        return check_case(case,output/'cases',harness,env)
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results=list(pool.map(evaluate,cases))
    negative=negatives(output,worker,env,output/'fixtures/blank.pptx',harness)
    summary=dict(reference='LibreOffice 26.8.1.1 / LibreOfficeKit',cases=results,negative=negative,passed=sum(r['passed'] for r in results),total=len(results),limitations=['This is a finite regression matrix, not exhaustive proof of all PPTX features.','Encrypted presentations, macros, SmartArt, 3D objects, embedded OLE and recording are not certified by this suite.'])
    (output/'report.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2))
    lines=['# Stage PPTX compatibility report','',f"Reference: {summary['reference']}",'',f"{summary['passed']}/{summary['total']} presentation cases passed all checks.",'','| Case | Result | Failed checks |','|---|---|---|']
    lines += [f"| {r['name']} | {'PASS' if r['passed'] else 'FAIL'} | {'; '.join(r['issues']).replace('|','/').replace(chr(10),' ')} |" for r in results]
    lines += ['','## Error handling','']+[f"- {r['name']}: {'PASS' if r['passed'] else 'FAIL'}" for r in negative]
    lines += ['','## Scope','']+['- '+v for v in summary['limitations']]
    (output/'REPORT.md').write_text('\n'.join(lines)+'\n')
    print(f"SUMMARY {summary['passed']}/{summary['total']} cases; report: {output/'REPORT.md'}",flush=True)
    return 0 if all(r['passed'] for r in results+negative) else 1
if __name__=='__main__':sys.exit(main())
