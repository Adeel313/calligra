#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Native regressions for cached diagrams, notes and XML paragraph identity."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import zipfile
from lxml import etree as E
from PIL import Image
from pptx import Presentation
from pptx.enum.text import MSO_AUTO_SIZE
from smartart_regressions import fixture

ROOT=Path(__file__).resolve().parents[4]
P='http://schemas.openxmlformats.org/presentationml/2006/main'
A='http://schemas.openxmlformats.org/drawingml/2006/main'
D='http://schemas.microsoft.com/office/drawing/2008/diagram'
R='http://schemas.openxmlformats.org/officeDocument/2006/relationships'


def main():
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    install=ROOT/'destdir/usr';env=os.environ.copy();env.update(QT_QPA_PLATFORM='offscreen',QT_QPA_PLATFORMTHEME='generic',QT_LOGGING_RULES='*.debug=false',GSETTINGS_BACKEND='memory',LD_LIBRARY_PATH=str(ROOT/'build/bin')+':'+str(install/'lib/x86_64-linux-gnu'),QT_PLUGIN_PATH=str(install/'lib/x86_64-linux-gnu/qt6/plugins'),XDG_DATA_DIRS=str(install/'share')+':/usr/local/share:/usr/share')
    source=out/'cached.pptx';fixture(source)
    with zipfile.ZipFile(source) as package:parts={n:package.read(n) for n in package.namelist()}
    tree=E.fromstring(parts['ppt/diagrams/drawing1.xml']);props=tree.find('.//{'+D+'}grpSpPr')
    for child in list(props):props.remove(child)
    shapes=tree.find('{'+D+'}spTree')
    pic=E.SubElement(shapes,'{'+D+'}pic');nv=E.SubElement(pic,'{'+D+'}nvPicPr');E.SubElement(nv,'{'+D+'}cNvPr',id='20',name='Cached image');E.SubElement(nv,'{'+D+'}cNvPicPr')
    fill=E.SubElement(pic,'{'+D+'}blipFill');E.SubElement(fill,'{'+A+'}blip',{'{'+R+'}embed':'rIdImage'});stretch=E.SubElement(fill,'{'+A+'}stretch');E.SubElement(stretch,'{'+A+'}fillRect')
    props=E.SubElement(pic,'{'+D+'}spPr');transform=E.SubElement(props,'{'+A+'}xfrm');E.SubElement(transform,'{'+A+'}off',x='0',y='0');E.SubElement(transform,'{'+A+'}ext',cx='914400',cy='914400');geometry=E.SubElement(props,'{'+A+'}prstGeom',prst='rect');E.SubElement(geometry,'{'+A+'}avLst')
    image=io.BytesIO();Image.new('RGB',(20,20),'blue').save(image,'PNG');payload=image.getvalue()
    parts['ppt/diagrams/drawing1.xml']=E.tostring(tree)
    parts['ppt/media/cached.png']=payload
    parts['ppt/diagrams/_rels/drawing1.xml.rels']=f'<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rIdImage" Type="{R}/image" Target="../media/cached.png"/></Relationships>'.encode()
    with zipfile.ZipFile(source,'w',zipfile.ZIP_DEFLATED) as package:
        for name,data in parts.items():package.writestr(name,data)
    before=hashlib.sha256(source.read_bytes()).hexdigest();converted=out/'cached.odp'
    with (out/'cached.log').open('wb') as log:subprocess.run([ROOT/'build/bin/calligraconverter','--batch',source,converted],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
    with zipfile.ZipFile(converted) as package:
        root=E.fromstring(package.read('content.xml'))
        assert ''.join(root.itertext()).count('Cached SmartArt sentinel')==1,'Cached label missing or duplicated'
        assert any(package.read(n)==payload for n in package.namelist() if n.startswith('Pictures/')),'Diagram image relationship lost'
    assert hashlib.sha256(source.read_bytes()).hexdigest()==before
    notes=out/'notes.pptx';deck=Presentation()
    for i in range(2):
        slide=deck.slides.add_slide(deck.slide_layouts[5]);slide.shapes.title.text='PAGE_'+str(i);slide.shapes.title.text_frame.auto_size=MSO_AUTO_SIZE.TEXT_TO_FIT_SHAPE if i==0 else None;slide.notes_slide.notes_text_frame.text='NOTE_'+str(i)
    deck.save(notes)
    with zipfile.ZipFile(notes) as package: parts={n:package.read(n) for n in package.namelist()}
    root=E.fromstring(parts['ppt/slides/slide1.xml'])
    shape=root.find('.//{'+P+'}sp')
    paragraph=shape.find('.//{'+A+'}p')
    paragraph.append(E.fromstring(f'<a14:m xmlns:a14="http://schemas.microsoft.com/office/drawing/2010/main" xmlns:m="http://schemas.openxmlformats.org/officeDocument/2006/math"><m:oMath><m:sSup><m:e><m:r><m:t>FORMULA</m:t></m:r></m:e><m:sup><m:r><m:t>2</m:t></m:r></m:sup></m:sSup></m:oMath></a14:m>'))
    mc='http://schemas.openxmlformats.org/markup-compatibility/2006'
    alternate=E.Element('{'+mc+'}AlternateContent',nsmap={'mc':mc,'a14':'http://schemas.microsoft.com/office/drawing/2010/main'})
    choice=E.SubElement(alternate,'{'+mc+'}Choice',Requires='a14')
    shape.getparent().replace(shape,alternate);choice.append(shape);E.SubElement(alternate,'{'+mc+'}Fallback')
    parts['ppt/slides/slide1.xml']=E.tostring(root)
    # Fitted notes must remain on the notes page and be saved exactly once.
    for name in list(parts):
        if name.startswith('ppt/notesSlides/notesSlide') and name.endswith('.xml'):
            note = E.fromstring(parts[name])
            for props in note.findall('.//{'+A+'}bodyPr'):
                E.SubElement(props, '{'+A+'}normAutofit', fontScale='85000', lnSpcReduction='20000')
            parts[name] = E.tostring(note)
    with zipfile.ZipFile(notes,'w',zipfile.ZIP_DEFLATED) as package:
        for name,data in parts.items(): package.writestr(name,data)
    result=out/'notes-result' 
    with (out/'notes.log').open('wb') as log:subprocess.run([ROOT/'build/bin/stage_pptx_roundtrip',notes,result],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
    with zipfile.ZipFile(result/'stage.odp') as package:
        saved=E.fromstring(package.read('content.xml'))
        assert not saved.xpath('//*[@*[local-name()="fit-to-size"]="true"]'), 'Uniform autofit saved as glyph stretching'
        assert saved.xpath('//*[@*[local-name()="shrink-to-fit"]="true"]'), 'Explicit shrinking lost'

    with zipfile.ZipFile(result/'stage.pptx') as package:
        slide=E.fromstring(package.read('ppt/slides/slide1.xml'))
        assert slide.find('.//{'+A+'}normAutofit') is not None, 'Explicit autofit lost'
        implicit=E.fromstring(package.read('ppt/slides/slide2.xml'))
        assert implicit.find('.//{'+A+'}normAutofit') is None, 'Absent autofit inherited shrinking'
        assert 'PAGE_0FORMULA2' in ''.join(slide.xpath('//*[local-name()="t"]/text()')), 'Math in a shape choice lost without SmartArt'
        text=''.join(E.fromstring(package.read(n)).xpath('//*[local-name()="t"]/text()')[0] for n in package.namelist() if n.startswith('ppt/notesSlides/notesSlide') and n.endswith('.xml'))
        assert 'NOTE_0' in text and 'NOTE_1' in text,'Notes lost on native round trip'
    (out/'result.json').write_text(json.dumps(dict(cached_diagram=True,cached_image=True,source_unchanged=True,notes=True,xml_ids=True),indent=2))
    print('PASS: native cached drawing, missing transform, diagram image links, notes and unique XML IDs')


if __name__=='__main__':main()
