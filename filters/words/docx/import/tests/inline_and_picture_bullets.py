#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Check inline picture placement after floating-image reflow and VML bullets."""
import argparse
import io
import os
from pathlib import Path
import subprocess
import zipfile

from docx import Document
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Pt, Inches
from lxml import etree as E
from PIL import Image
import pymupdf

ROOT = Path(__file__).resolve().parents[5]
W = 'http://schemas.openxmlformats.org/wordprocessingml/2006/main'
R = 'http://schemas.openxmlformats.org/officeDocument/2006/relationships'
V = 'urn:schemas-microsoft-com:vml'


def bitmap(color, kind='PNG'):
    data = io.BytesIO()
    Image.new('RGB', (24, 24), color).save(data, format=kind)
    return data.getvalue()


def floating(run, color, side):
    inline = run.add_picture(io.BytesIO(bitmap(color)), width=Pt(72), height=Pt(72))._inline
    anchor = E.Element(qn('wp:anchor'), distT='0', distB='0', distL='114300', distR='114300',
                       simplePos='0', relativeHeight='1', behindDoc='0', locked='0', layoutInCell='1', allowOverlap='1')
    E.SubElement(anchor, qn('wp:simplePos'), x='0', y='0')
    h = E.SubElement(anchor, qn('wp:positionH'), relativeFrom='margin')
    E.SubElement(h, qn('wp:align')).text = side
    v = E.SubElement(anchor, qn('wp:positionV'), relativeFrom='paragraph')
    E.SubElement(v, qn('wp:posOffset')).text = '0'
    for child in list(inline):
        if child.tag == qn('wp:docPr'):
            E.SubElement(anchor, qn('wp:wrapSquare'), wrapText='bothSides')
        anchor.append(child)
    inline.getparent().replace(inline, anchor)


def check(output):
    output.mkdir(parents=True, exist_ok=False)
    doc = Document()
    section = doc.sections[0]
    section.page_width = Inches(8.5)
    section.left_margin = section.right_margin = Inches(1)
    doc.styles['Normal'].font.name = 'DejaVu Sans'
    doc.styles['Normal'].font.size = Pt(12)
    p = doc.add_paragraph('Images can be inline pictures in the normal text flow, such as this green dot ')
    p.add_run().add_picture(io.BytesIO(bitmap('green')), width=Pt(10.8), height=Pt(10.8))
    p.add_run('. The surrounding floating pictures change the available line width. ')
    floating(p.add_run(), 'blue', 'left')
    p.add_run('Text wraps beside both floating pictures. ' * 8)
    floating(p.add_run(), 'orange', 'right')
    p = doc.add_paragraph('Picture bullet item')
    num = OxmlElement('w:numPr')
    E.SubElement(num, qn('w:ilvl'), {qn('w:val'): '0'})
    E.SubElement(num, qn('w:numId'), {qn('w:val'): '99'})
    p._p.get_or_add_pPr().append(num)
    source, pdf = output / 'pictures.docx', output / 'pictures.pdf'
    doc.save(source)
    with zipfile.ZipFile(source) as package:
        entries = {name: package.read(name) for name in package.namelist()}
    root = E.fromstring(entries['word/numbering.xml'])
    picture = E.Element(qn('w:numPicBullet'), {qn('w:numPicBulletId'): '99'})
    pict = E.SubElement(picture, qn('w:pict'))
    E.SubElement(pict, '{%s}shapetype' % V, id='_x0000_t75', coordsize='21600,21600',
                 **{'{urn:schemas-microsoft-com:office:office}spt': '75'})
    shape = E.SubElement(pict, '{%s}shape' % V, id='bullet', type='#_x0000_t75', style='width:9pt;height:9pt')
    E.SubElement(shape, '{%s}imagedata' % V, {'{%s}id' % R: 'pictureBullet'})
    root.insert(0, picture)
    abstract = E.SubElement(root, qn('w:abstractNum'), {qn('w:abstractNumId'): '99'})
    root.remove(abstract)
    root.insert(next((i for i, child in enumerate(root) if child.tag == qn('w:num')), len(root)), abstract)
    level = E.SubElement(abstract, qn('w:lvl'), {qn('w:ilvl'): '0'})
    for name, value in [('start', '1'), ('numFmt', 'bullet'), ('lvlText', '•'), ('lvlPicBulletId', '99')]:
        E.SubElement(level, qn('w:' + name), {qn('w:val'): value})
    props = E.SubElement(level, qn('w:pPr'))
    E.SubElement(props, qn('w:ind'), {qn('w:left'): '720', qn('w:hanging'): '360'})
    num = E.SubElement(root, qn('w:num'), {qn('w:numId'): '99'})
    E.SubElement(num, qn('w:abstractNumId'), {qn('w:val'): '99'})
    entries['word/numbering.xml'] = E.tostring(root)
    rels = E.Element('Relationships', xmlns='http://schemas.openxmlformats.org/package/2006/relationships')
    E.SubElement(rels, 'Relationship', Id='pictureBullet', Type=R + '/image', Target='media/bullet.gif')
    entries['word/_rels/numbering.xml.rels'] = E.tostring(rels)
    entries['word/media/bullet.gif'] = bitmap('magenta', 'GIF')
    types = E.fromstring(entries['[Content_Types].xml'])
    E.SubElement(types, '{%s}Default' % types.nsmap[None], Extension='gif', ContentType='image/gif')
    entries['[Content_Types].xml'] = E.tostring(types)
    with zipfile.ZipFile(source, 'w') as package:
        for name, data in entries.items(): package.writestr(name, data)
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', XDG_CONFIG_HOME=str(output / 'config'))
    with (output / 'export.log').open('w') as log:
        subprocess.run([ROOT / 'run-words.sh', '--export-pdf', '--export-filename', pdf, source],
                       env=env, stdout=log, stderr=log, check=True, timeout=90)
    with pymupdf.open(pdf) as rendered:
        assert len(rendered) == 1, len(rendered)
        page = rendered[0]
        images = [pymupdf.Rect(info['bbox']) for info in page.get_image_info()]
        dot = next(rect for rect in images if abs(rect.width - 10.8) < 0.2)
        marker = page.search_for('green dot')[-1]
        assert 0 <= dot.x0 - marker.x1 < 5 and abs(dot.y1 - marker.y1) < 5, (dot, marker)
        bullet = next(rect for rect in images if abs(rect.width - 9) < 0.2)
        item = page.search_for('Picture bullet item')[0]
        assert bullet.x1 < item.x0 and abs(bullet.y0 - item.y0) < 8, (bullet, item)
    print('PASS: inline image follows its text after reflow; picture bullet renders at its declared 9pt size')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    check(parser.parse_args().output.resolve())
