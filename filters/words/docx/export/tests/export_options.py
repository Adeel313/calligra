#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Compare native DOCX defaults and run formatting with LibreOffice on common ODT."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import zipfile
from lxml import etree as E

ROOT = Path(__file__).resolve().parents[5]
REFERENCE = ROOT/'filters/stage/pptx/tests/libreoffice_reference.py'
W = 'http://schemas.openxmlformats.org/wordprocessingml/2006/main'
NS = {'w':W}
ODF = '''xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0"
xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"
xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"
xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0"'''
ODF += ' xmlns:xlink="http://www.w3.org/1999/xlink"'


def environment():
    install=ROOT/'destdir/usr'
    return dict(os.environ,QT_QPA_PLATFORM='offscreen',QT_QPA_PLATFORMTHEME='generic',
        QT_LOGGING_RULES='*.debug=false',GSETTINGS_BACKEND='memory',SAL_USE_VCLPLUGIN='svp',
        LD_LIBRARY_PATH=str(ROOT/'build/bin')+':'+str(install/'lib/x86_64-linux-gnu'),
        QT_PLUGIN_PATH=str(install/'lib/x86_64-linux-gnu/qt6/plugins'),
        XDG_DATA_DIRS=str(install/'share')+':/usr/local/share:/usr/share')


def fixture(path):
    styles=[('Format','', 'style:font-name="LatinBody" style:font-name-asian="AsianFace" style:font-name-complex="ScriptFace" fo:font-size="20pt" style:font-size-complex="18pt" fo:language="en" fo:country="US" style:language-asian="ja" style:country-asian="JP" style:language-complex="ar" style:country-complex="SA"'),
        ('UnderlineDouble','', 'style:text-underline-style="solid" style:text-underline-type="double" style:text-underline-color="#aa1122"'),
        ('UnderlineWords','', 'style:text-underline-style="solid" style:text-underline-mode="skip-white-space"'),
        ('Caps','', 'fo:text-transform="uppercase"'),('CapsReset','Caps','fo:text-transform="none"'),
        ('Scale','', 'style:text-scale="75%"'),('ScaleReset','Scale','style:text-scale="100%"'),
        ('Spaced','', 'fo:letter-spacing="2pt"'),('SpacingReset','Spaced','fo:letter-spacing="normal"'),
        ('Position','', 'fo:font-size="12pt" style:text-position="25% 100%"'),
        ('PositionReset','Position','style:text-position="0% 100%"'),
        ('Super','', 'fo:font-size="12pt" style:text-position="super 58%"'),
        ('Hidden','', 'text:display="none"')]
    named=''.join(f'<style:style style:name="{name}" style:family="text"'+
                 (f' style:parent-style-name="{parent}"' if parent else '')+
                 f'><style:text-properties {props}/></style:style>' for name,parent,props in styles)
    paragraphs='<text:p>DEFAULT_SENTINEL</text:p>'
    for name,_,_ in styles:
        paragraphs+=f'<text:p><text:span text:style-name="{name}">{name}_SENTINEL</text:span></text:p>'
    paragraphs+='<text:p text:style-name="KeepReset">KEEP_RESET</text:p><text:p>SOFT_LEFT<text:soft-page-break/>SOFT_RIGHT</text:p>'
    paragraphs+='''<text:p><text:span text:style-name="Spaced">OUTER_A<text:span text:style-name="CapsReset">innerMixed</text:span>OUTER_B<text:a xlink:href="https://example.org/docx-test"><text:span text:style-name="CapsReset">LINK_SENTINEL</text:span></text:a>OUTER_C</text:span></text:p>
<text:p>NOTE_BODY<text:note text:id="note1" text:note-class="footnote"><text:note-citation>1</text:note-citation><text:note-body><text:p><text:span text:style-name="Spaced">NOTE_OUTER<text:span text:style-name="CapsReset">NOTE_INNER</text:span></text:span></text:p></text:note-body></text:note></text:p>'''
    style_doc=f'''<office:document-styles {ODF} office:version="1.2">
<office:font-face-decls><style:font-face style:name="LatinBody" svg:font-family="'Liberation Serif'"/>
<style:font-face style:name="AsianFace" svg:font-family="'DejaVu Sans'"/>
<style:font-face style:name="ScriptFace" svg:font-family="'DejaVu Sans'"/></office:font-face-decls>
<office:styles><style:default-style style:family="paragraph"><style:paragraph-properties fo:margin-top="0pt" fo:margin-bottom="0pt"/>
<style:text-properties style:font-name="LatinBody" fo:font-size="11pt" fo:language="en" fo:country="US"/></style:default-style>
{named}<style:style style:name="Keep" style:family="paragraph"><style:paragraph-properties fo:keep-with-next="always" fo:keep-together="always"/></style:style>
<style:style style:name="KeepReset" style:family="paragraph" style:parent-style-name="Keep"><style:paragraph-properties fo:keep-with-next="auto" fo:keep-together="auto"/></style:style></office:styles>
<office:automatic-styles><style:page-layout style:name="Page"><style:page-layout-properties fo:page-width="612pt" fo:page-height="792pt" fo:margin="72pt"/></style:page-layout></office:automatic-styles>
<office:master-styles><style:master-page style:name="Standard" style:page-layout-name="Page"/></office:master-styles></office:document-styles>'''
    with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED) as package:
        package.writestr('mimetype','application/vnd.oasis.opendocument.text')
        package.writestr('styles.xml',style_doc)
        package.writestr('content.xml',f'<office:document-content {ODF} office:version="1.2"><office:body><office:text>{paragraphs}</office:text></office:body></office:document-content>')
        package.writestr('meta.xml',f'<office:document-meta {ODF}><office:meta/></office:document-meta>')
        package.writestr('META-INF/manifest.xml','''<manifest:manifest xmlns:manifest="urn:oasis:names:tc:opendocument:xmlns:manifest:1.0" manifest:version="1.2">
<manifest:file-entry manifest:full-path="/" manifest:media-type="application/vnd.oasis.opendocument.text"/>
<manifest:file-entry manifest:full-path="content.xml" manifest:media-type="text/xml"/>
<manifest:file-entry manifest:full-path="styles.xml" manifest:media-type="text/xml"/>
<manifest:file-entry manifest:full-path="meta.xml" manifest:media-type="text/xml"/></manifest:manifest>''')


def effective(path):
    with zipfile.ZipFile(path) as package:
        document=E.fromstring(package.read('word/document.xml'));styles=E.fromstring(package.read('word/styles.xml'))
    byid={s.get('{'+W+'}styleId'):s for s in styles.findall('w:style',NS)}
    defaults=styles.find('w:docDefaults/w:rPrDefault/w:rPr',NS)
    def lineage(identifier,seen=None):
        seen=set() if seen is None else seen
        if not identifier or identifier in seen or identifier not in byid:return []
        seen.add(identifier);style=byid[identifier];parent=style.find('w:basedOn',NS)
        return lineage(parent.get('{'+W+'}val') if parent is not None else None,seen)+[style]
    result={}
    for paragraph in document.findall('.//w:p',NS):
        pp=paragraph.find('w:pPr',NS);ps=pp.find('w:pStyle',NS) if pp is not None else None
        ancestors=lineage(ps.get('{'+W+'}val') if ps is not None else None)
        for run in paragraph.findall('w:r',NS):
            text=''.join(run.xpath('./w:t/text()',namespaces=NS));rp=run.find('w:rPr',NS)
            rs=rp.find('w:rStyle',NS) if rp is not None else None
            nodes=([defaults] if defaults is not None else [])+[s.find('w:rPr',NS) for s in ancestors]
            nodes += [s.find('w:rPr',NS) for s in lineage(rs.get('{'+W+'}val') if rs is not None else None)]+[rp]
            props={}
            for node in nodes:
                if node is not None:
                    for child in node:
                        props[E.QName(child).localname]={E.QName(k).localname:v for k,v in child.attrib.items()}
            if text:result[text]=props
        if 'KEEP_RESET' in ''.join(paragraph.xpath('.//w:t/text()',namespaces=NS)):
            values={}
            for node in [s.find('w:pPr',NS) for s in ancestors]+[pp]:
                if node is not None:
                    for child in node:values[E.QName(child).localname]=child.get('{'+W+'}val','1')
            result['KEEP_PROPERTIES']=values
    return result,document,styles


def checks(native,reference):
    n,doc,styles=effective(native);r,rdoc,rstyles=effective(reference)
    out={}
    def compare(label,token,tag,attribute='val',default=None):
        def value(props):
            if tag not in props:return default
            result=props[tag].get(attribute,'1' if tag in ('caps','smallCaps','vanish','b','i') else default)
            return '0' if result in ('false','off') else '1' if result in ('true','on') else result
        a=value(n[token]);b=value(r[token])
        if attribute=='color':a=a.upper() if a else a;b=b.upper() if b else b
        out[label]=dict(passed=a==b,native=a,reference=b)
    for key in ['ascii','hAnsi','eastAsia','cs']:compare('font_'+key,'Format_SENTINEL','rFonts',key)
    compare('complex_size','Format_SENTINEL','szCs')
    for key in ['val','eastAsia','bidi']:compare('language_'+key,'Format_SENTINEL','lang',key)
    compare('default_font','DEFAULT_SENTINEL','rFonts','ascii')
    compare('default_size','DEFAULT_SENTINEL','sz')
    compare('double_underline','UnderlineDouble_SENTINEL','u')
    compare('underline_color','UnderlineDouble_SENTINEL','u','color')
    compare('words_underline','UnderlineWords_SENTINEL','u')
    compare('caps_reset','CapsReset_SENTINEL','caps',default='0')
    compare('scale_reset','ScaleReset_SENTINEL','w',default='100')
    compare('spacing_reset','SpacingReset_SENTINEL','spacing',default='0')
    compare('raised_position','Position_SENTINEL','vertAlign')
    compare('position_reset','PositionReset_SENTINEL','position',default='0')
    compare('baseline_reset','PositionReset_SENTINEL','vertAlign',default='baseline')
    compare('superscript','Super_SENTINEL','vertAlign')
    compare('hidden','Hidden_SENTINEL','vanish',default='0')
    for key in ['keepNext','keepLines']:
        a=n['KEEP_PROPERTIES'].get(key,'0');b=r['KEEP_PROPERTIES'].get(key,'0')
        a='0' if a in ('false','off') else '1' if a in ('true','on') else a
        b='0' if b in ('false','off') else '1' if b in ('true','on') else b
        out[key+'_reset']=dict(passed=a==b,native=a,reference=b)
    valid=styles.find('w:docDefaults/w:rPrDefault/w:rPr',NS) is not None and styles.find('w:docDefaults/w:pPrDefault/w:pPr/w:rPr',NS) is None
    out['valid_document_defaults']=dict(passed=valid,native=valid,reference=True)
    a=len(doc.xpath('//w:br[@w:type="page"]',namespaces=NS));b=len(rdoc.xpath('//w:br[@w:type="page"]',namespaces=NS))
    out['automatic_page_breaks']=dict(passed=a==b,native=a,reference=b)
    with zipfile.ZipFile(native) as package:
        nested=[]
        for part in package.namelist():
            if part.startswith('word/') and part.endswith('.xml'):
                root=E.fromstring(package.read(part))
                nested.extend(root.xpath('//w:r/w:r',namespaces=NS))
        rels=E.fromstring(package.read('word/_rels/document.xml.rels'))
        linked=any(rel.get('Target')=='https://example.org/docx-test' for rel in rels)
        notes=E.fromstring(package.read('word/footnotes.xml'))
        note_text=''.join(notes.xpath('//w:t/text()',namespaces=NS))
    out['flat_runs']=dict(passed=not nested,native=len(nested),reference=0)
    out['nested_link']=dict(passed=linked,native=linked,reference=True)
    nested_native=n.get('innerMixed',{}).get('spacing',{}).get('val','0')
    nested_reference=r.get('innerMixed',{}).get('spacing',{}).get('val','0')
    out['nested_spacing']=dict(passed='innerMixed' in n and nested_native==nested_reference,native=nested_native,reference=nested_reference)
    out['nested_note']=dict(passed=note_text=='NOTE_OUTERNOTE_INNER',native=note_text,reference='NOTE_OUTERNOTE_INNER')
    return out


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    source=out/'common.odt';fixture(source)
    commands=[[ROOT/'build/bin/calligraconverter','--batch',source,out/'native.docx'],
        ['/usr/bin/python3',REFERENCE,source,out/'reference.docx'],
        ['/usr/bin/python3',REFERENCE,out/'reference.docx',out/'reference.pdf'],
        ['/usr/bin/python3',REFERENCE,out/'native.docx',out/'native.pdf']]
    for index,command in enumerate(commands):
        with (out/f'conversion-{index}.log').open('wb') as log:
            subprocess.run(command,env=environment(),stdout=log,stderr=log,check=True,timeout=180)
    result=checks(out/'native.docx',out/'reference.docx')
    import fitz
    from PIL import Image, ImageChops, ImageStat
    expected,actual=fitz.open(out/'reference.pdf'),fitz.open(out/'native.pdf')
    native_text=''.join(''.join(page.get_text().split()) for page in actual)
    required=['innerMixed','LINK_SENTINEL','NOTE_INNER']
    result['reopened_nested_text']=dict(passed=all(token in native_text for token in required),native=native_text,reference=required)
    scores=[]
    for index in range(min(len(expected),len(actual))):
        ep=expected[index].get_pixmap(alpha=False);ap=actual[index].get_pixmap(alpha=False)
        e=Image.frombytes('RGB',(ep.width,ep.height),ep.samples)
        a=Image.frombytes('RGB',(ap.width,ap.height),ap.samples).resize(e.size)
        difference=ImageChops.difference(e,a)
        scores.append(1-sum(ImageStat.Stat(difference).mean)/(3*255))
        e.save(out/f'reference-{index+1}.png');a.save(out/f'native-{index+1}.png');difference.save(out/f'difference-{index+1}.png')
    result['rendering']=dict(passed=len(expected)==len(actual) and all(s>=.95 for s in scores),native=scores,reference='equal page counts and >=0.95 per page')
    report=dict(passed=all(c['passed'] for c in result.values()),checks=result)
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2));return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
