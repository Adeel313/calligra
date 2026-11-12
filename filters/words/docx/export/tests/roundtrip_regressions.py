#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Check native DOCX round trips for revisions, links, text boxes and field caches."""
import argparse
import copy
import json
from pathlib import Path
import subprocess
import zipfile
from lxml import etree as E
from docx import Document
from export_options import ROOT, environment

W='http://schemas.openxmlformats.org/wordprocessingml/2006/main'
R='http://schemas.openxmlformats.org/officeDocument/2006/relationships'
NS={'w':W,'r':R}
def xml(value):return E.fromstring(value.encode())
def run(output):
    output.mkdir(parents=True,exist_ok=False)
    source=output/'input.docx'
    document=Document()
    document.add_paragraph('Visible text: ')
    document.sections[0].footer.paragraphs[0].text='Page '
    document.save(source)
    with zipfile.ZipFile(source) as z:parts={n:z.read(n) for n in z.namelist()}
    root=E.fromstring(parts['word/document.xml']);p=root.find('.//w:p',NS)
    p.append(xml(f'<w:ins xmlns:w="{W}" w:id="5" w:author="Insert Author" w:date="2026-01-02T03:04:05Z"><w:r><w:t>Inserted</w:t></w:r></w:ins>'))
    p.append(xml(f'<w:del xmlns:w="{W}" w:id="6" w:author="Delete Author" w:date="2026-02-03T04:05:06Z"><w:r><w:rPr><w:b/></w:rPr><w:delText>Removed</w:delText></w:r></w:del>'))
    p.append(xml(f'<w:hyperlink xmlns:w="{W}" xmlns:r="{R}" r:id="rIdRoundtrip" w:anchor="Target"><w:r><w:t>External link</w:t></w:r></w:hyperlink>'))
    p.append(xml(f'''<w:r xmlns:w="{W}" xmlns:wp="http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing" xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" xmlns:wps="http://schemas.microsoft.com/office/word/2010/wordprocessingShape"><w:drawing><wp:inline><wp:extent cx="3240000" cy="792000"/><wp:docPr id="91" name="Textbox"/><a:graphic><a:graphicData uri="http://schemas.microsoft.com/office/word/2010/wordprocessingShape"><wps:wsp><wps:cNvSpPr txBox="1"/><wps:spPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="3240000" cy="792000"/></a:xfrm><a:prstGeom prst="roundRect"><a:avLst/></a:prstGeom></wps:spPr><wps:txbx><w:txbxContent><w:p><w:r><w:rPr><w:b/></w:rPr><w:t>Textbox sentinel</w:t></w:r></w:p></w:txbxContent></wps:txbx></wps:wsp></a:graphicData></a:graphic></wp:inline></w:drawing></w:r>'''))
    alternateRun = copy.deepcopy(p[-1])
    drawing = alternateRun[0]
    alternateRun.remove(drawing)
    drawing.xpath('.//w:t',namespaces=NS)[0].text='Alternate textbox sentinel'
    docPr = drawing.find('.//{http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing}docPr')
    docPr.set('id','92');docPr.set('name','Alternate Textbox')
    mc='http://schemas.openxmlformats.org/markup-compatibility/2006'
    ac=E.SubElement(alternateRun,'{'+mc+'}AlternateContent',nsmap={'mc':mc,'wps':'http://schemas.microsoft.com/office/word/2010/wordprocessingShape'})
    choice=E.SubElement(ac,'{'+mc+'}Choice',Requires='wps');choice.append(drawing)
    E.SubElement(ac,'{'+mc+'}Fallback')
    p.append(alternateRun)
    parts['word/document.xml']=E.tostring(root)
    rels=E.fromstring(parts['word/_rels/document.xml.rels'])
    E.SubElement(rels,'{http://schemas.openxmlformats.org/package/2006/relationships}Relationship',Id='rIdRoundtrip',Type=R+'/hyperlink',Target='https://example.org/report',TargetMode='External')
    parts['word/_rels/document.xml.rels']=E.tostring(rels)
    footer=E.fromstring(parts['word/footer1.xml']);fp=footer.find('w:p',NS)
    for instruction,cached in [('PAGE','17'),('NUMPAGES','42')]:
        for markup in ['<w:fldChar w:fldCharType="begin"/>',f'<w:instrText> {instruction} </w:instrText>','<w:fldChar w:fldCharType="separate"/>',f'<w:t>{cached}</w:t>','<w:fldChar w:fldCharType="end"/>']:
            fp.append(xml(f'<w:r xmlns:w="{W}">{markup}</w:r>'))
    parts['word/footer1.xml']=E.tostring(footer)
    with zipfile.ZipFile(source,'w',zipfile.ZIP_DEFLATED) as z:
        for name,data in parts.items():z.writestr(name,data)
    env=environment()
    for first,last,name in [(source,output/'imported.odt','import'),(output/'imported.odt',output/'roundtrip.docx','export')]:
        with (output/(name+'.log')).open('wb') as log:
            subprocess.run([ROOT/'build/bin/calligraconverter','--batch',first,last],env=env,stdout=log,stderr=log,check=True,timeout=60)
    with zipfile.ZipFile(output/'roundtrip.docx') as z:
        doc=E.fromstring(z.read('word/document.xml'))
        links=E.fromstring(z.read('word/_rels/document.xml.rels'))
        footers=[E.fromstring(z.read(n)) for n in z.namelist() if n.startswith('word/footer') and n.endswith('.xml')]
        checks={
            'deletion_remains_a_revision':doc.xpath('//w:del/w:r/w:delText/text()',namespaces=NS)==['Removed'],
            'deleted_text_not_in_body':'Removed' not in ''.join(doc.xpath('//w:t/text()',namespaces=NS)),
            'deleted_run_formatting':bool(doc.xpath('//w:del/w:r/w:rPr/w:b',namespaces=NS)),
            'insertion_author':doc.xpath('//w:ins/@w:author',namespaces=NS)==['Insert Author'],
            'deletion_author':doc.xpath('//w:del/@w:author',namespaces=NS)==['Delete Author'],
            'revision_dates':set(doc.xpath('//w:ins/@w:date | //w:del/@w:date',namespaces=NS))=={'2026-01-02T03:04:05Z','2026-02-03T04:05:06Z'},
            'external_link_and_anchor':any(n.get('Target')=='https://example.org/report' for n in links) and bool(doc.xpath('//w:hyperlink[@w:anchor="Target"]',namespaces=NS)),
            'textbox_text':'Textbox sentinel' in ''.join(doc.xpath('//w:txbxContent//w:t/text()',namespaces=NS)),
            'alternate_textbox_text':'Alternate textbox sentinel' in ''.join(doc.xpath('//w:txbxContent//w:t/text()',namespaces=NS)),
            'textbox_formatting':bool(doc.xpath('//w:txbxContent//w:r[w:t="Textbox sentinel"]/w:rPr/w:b',namespaces=NS)),
            'cached_page_values':any('1742' in ''.join(f.xpath('//w:t/text()',namespaces=NS)) for f in footers),
            'valid_package':z.testzip() is None,
        }
    (output/'report.json').write_text(json.dumps(checks,indent=2)+'\n')
    print(json.dumps(checks,indent=2),flush=True)
    return 0 if all(checks.values()) else 1
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True)
    raise SystemExit(run(parser.parse_args().output.resolve()))
