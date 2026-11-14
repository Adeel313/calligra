#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Compare native and LibreOffice DOCX exports of the same ODT, independently of import."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import zipfile
import fitz
from lxml import etree as E
from PIL import Image, ImageChops, ImageStat

ROOT=Path(__file__).resolve().parents[2]
REFERENCE=ROOT/'filters/stage/pptx/tests/libreoffice_reference.py'
W='http://schemas.openxmlformats.org/wordprocessingml/2006/main'
NS={'w':W}


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def normalize(text):return ''.join(text.split())


def features(path):
    result=dict(text='',headers=[],footers=[],footnotes='',endnotes='',tables=[],images=0,links=[])
    with zipfile.ZipFile(path) as package:
        for part in package.namelist():
            if part.endswith('.rels'):
                for rel in E.fromstring(package.read(part)):
                    if rel.get('Type','').endswith('/hyperlink'):result['links'].append(rel.get('Target'))
            if not part.startswith('word/') or not part.endswith('.xml'):continue
            root=E.fromstring(package.read(part));text=''.join(root.xpath('//w:t/text()',namespaces=NS))
            if part=='word/document.xml':
                result['text']=text;result['images']=len(root.xpath('//*[local-name()="blip"]'))
                for table in root.xpath('//w:tbl',namespaces=NS):
                    result['tables'].append([[normalize(''.join(cell.xpath('.//w:t/text()',namespaces=NS))) for cell in row.findall('w:tc',NS)] for row in table.findall('w:tr',NS)])
            elif part.startswith('word/header'):result['headers'].append(normalize(text))
            elif part.startswith('word/footer'):result['footers'].append(normalize(text))
            elif part=='word/footnotes.xml':result['footnotes']=normalize(text)
            elif part=='word/endnotes.xml':result['endnotes']=normalize(text)
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('input',type=Path)
    parser.add_argument('--output',type=Path,required=True);parser.add_argument('--baseline',type=Path)
    args=parser.parse_args();source=args.input.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    spec=importlib.util.spec_from_file_location('docx_options',ROOT/'filters/words/docx/export/tests/export_options.py')
    options=importlib.util.module_from_spec(spec);spec.loader.exec_module(options);env=options.environment()
    before=digest(source)
    def run(command,name):
        with (out/(name+'.log')).open('wb') as log:
            subprocess.run(command,env=env,stdout=log,stderr=log,check=True,timeout=300)
    run([ROOT/'build/bin/calligraconverter','--batch',source,out/'native.docx'],'native-export')
    run(['/usr/bin/python3',REFERENCE,source,out/'reference.docx'],'reference-export')
    for name in ['native','reference']:
        run(['/usr/bin/python3',REFERENCE,out/(name+'.docx'),out/(name+'.pdf')],name+'-reopen')
    if args.baseline:run(['/usr/bin/python3',REFERENCE,args.baseline.resolve(),out/'baseline.pdf'],'baseline-reopen')
    native,reference=features(out/'native.docx'),features(out/'reference.docx')
    checks={key:native[key]==reference[key] for key in ['tables','footnotes','endnotes']}
    checks['text']=normalize(native['text'])==normalize(reference['text'])
    for key in ['headers','footers','links']:checks[key]=set(native[key])==set(reference[key])
    checks['images']=native['images']>=reference['images']
    def visual(name):
        expected,actual=fitz.open(out/'reference.pdf'),fitz.open(out/(name+'.pdf'))
        pages=[]
        for index in range(min(len(expected),len(actual))):
            ep=expected[index].get_pixmap(alpha=False);ap=actual[index].get_pixmap(alpha=False)
            e=Image.frombytes('RGB',(ep.width,ep.height),ep.samples)
            a=Image.frombytes('RGB',(ap.width,ap.height),ap.samples).resize(e.size)
            diff=ImageChops.difference(e,a);score=1-sum(ImageStat.Stat(diff).mean)/(3*255)
            mask=e.convert('L').point(lambda v:255 if v<245 else 0)
            foreground=1-sum(ImageStat.Stat(diff,mask).mean)/(3*255) if mask.getbbox() else None
            pages.append(dict(page=index+1,pixel_similarity=score,foreground_similarity=foreground))
            a.save(out/f'{name}-{index+1}.png');diff.save(out/f'{name}-difference-{index+1}.png')
            if name=='native':e.save(out/f'reference-{index+1}.png')
        return dict(pages=len(actual),reference_pages=len(expected),scores=pages,
                    passed=len(actual)==len(expected) and all(p['pixel_similarity']>=.92 for p in pages))
    rendered=visual('native');checks['page_count']=rendered['pages']==rendered['reference_pages'];checks['rendering']=rendered['passed']
    integrity={}
    for name in ['native','reference']:
        with zipfile.ZipFile(out/(name+'.docx')) as package:
            errors=[]
            for part in package.namelist():
                if part.endswith(('.xml','.rels')):
                    try:E.fromstring(package.read(part))
                    except E.XMLSyntaxError as error:errors.append(dict(part=part,error=str(error)))
            integrity[name]=dict(crc_pass=package.testzip() is None,xml_errors=errors)
    checks['package_integrity']=all(x['crc_pass'] and not x['xml_errors'] for x in integrity.values())
    checks['source_unchanged']=digest(source)==before
    report=dict(source=str(source),source_sha256=before,checks=checks,passed=all(checks.values()),native_features=native,
        reference_features=reference,visual=rendered,package_integrity=integrity,
        native_filter_sha256=digest(ROOT/'build/install/lib/x86_64-linux-gnu/qt6/plugins/calligra/formatfilters/calligra_filter_odt2docx.so'))
    if args.baseline:report['baseline_visual']=visual('baseline')
    report['reference_version']=subprocess.check_output(['/usr/bin/libreoffice','--version'],env=env,text=True).strip()
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    lines=['# Native DOCX export comparison','',report['reference_version'],'',
        'Both exporters read the same ODT. Both DOCX outputs are independently reopened and rendered by LibreOffice.','',
        '| Check | Result |','|---|---|']
    lines += [f"| {k} | {'PASS' if v else 'FAIL'} |" for k,v in checks.items()]
    lines += ['',f"Pages: native {rendered['pages']}; LibreOffice {rendered['reference_pages']}.",'',
        '| Page | Previous native similarity | Updated native similarity |','|---|---:|---:|']
    baseline=report.get('baseline_visual',{}).get('scores',[])
    for i,page in enumerate(rendered['scores']):
        old=f"{baseline[i]['pixel_similarity']:.5f}" if i<len(baseline) else 'not measured'
        lines.append(f"| {i+1} | {old} | {page['pixel_similarity']:.5f} |")
    lines += ['', 'The rendering gate requires equal page counts and at least 0.92 pixel similarity per page. Scores can hide localized errors and do not establish exact visual identity. Foreground scores and difference images are in this directory.', '',
        'Source SHA-256: `'+before+'`.']
    (out/'REPORT.md').write_text('\n'.join(lines)+'\n');print(out/'REPORT.md',flush=True)
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
