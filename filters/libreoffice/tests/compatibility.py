#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Compare real Words/Sheets document round trips with installed LibreOffice."""
import argparse
import concurrent.futures
import datetime
import json
import os
from pathlib import Path
import subprocess
import sys
import zipfile
from lxml import etree as E
from PIL import Image
from docx import Document
from docx.shared import Inches, Pt, RGBColor
from docx.enum.section import WD_SECTION_START, WD_ORIENT
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from openpyxl import Workbook, load_workbook
from openpyxl.styles import Font, PatternFill, Alignment
from openpyxl.worksheet.datavalidation import DataValidation
from openpyxl.formatting.rule import ColorScaleRule
from openpyxl.chart import BarChart, LineChart, PieChart, ScatterChart, Reference, Series
from openpyxl.drawing.image import Image as SheetImage
from openpyxl.workbook.defined_name import DefinedName

HERE=Path(__file__).resolve().parent
REFERENCE=HERE.parents[1]/'stage/pptx/tests/libreoffice_reference.py'

def run(args,env,log,timeout=90):
    with Path(log).open('wb') as stream:
        p=subprocess.run(list(map(str,args)),env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=timeout)
    if p.returncode:raise RuntimeError(f'exit {p.returncode}: '+Path(log).read_text(errors='replace')[-1500:])

def fixtures(root):
    root.mkdir(parents=True,exist_ok=True);cases=[]
    image=root/'sample.png';Image.new('RGB',(160,90),(20,100,180)).save(image)
    def save(name,doc,kind,tokens=(),checks=()):
        path=root/(name+'.'+kind);doc.save(path);cases.append(dict(name=name,path=str(path),kind=kind,tokens=list(tokens),checks=list(checks)))
    d=Document();save('words_empty',d,'docx')
    d=Document();d.add_paragraph('Unicode café اردو العربية 中文 Ελληνικά');save('words_unicode',d,'docx',['Unicode café اردو العربية 中文 Ελληνικά'])
    d=Document();p=d.add_paragraph();p.add_run('BOLD_SENTINEL').bold=True;r=p.add_run(' ITALIC_SENTINEL');r.italic=True;r.font.color.rgb=RGBColor(180,20,40);r.font.size=Pt(18);p.add_run(' UNDERLINE').underline=True;save('words_runs',d,'docx',['BOLD_SENTINEL','ITALIC_SENTINEL','UNDERLINE'],['bold','italic','underline'])
    d=Document();d.add_heading('HEADING_SENTINEL',1)
    for i in range(4):d.add_paragraph('LIST_ITEM_'+str(i),style='List Bullet' if i%2 else 'List Number')
    save('words_lists',d,'docx',['HEADING_SENTINEL','LIST_ITEM_3'])
    d=Document();t=d.add_table(rows=3,cols=3);t.style='Table Grid'
    for r in range(3):
        for c in range(3):t.cell(r,c).text=f'CELL_{r}_{c}'
    save('words_table',d,'docx',['CELL_0_0','CELL_2_2'],['tables'])
    d=Document();t=d.add_table(rows=3,cols=3);t.cell(0,0).merge(t.cell(0,2)).text='MERGED_HEADER';t.cell(1,0).text='MERGED_BODY';save('words_merged_table',d,'docx',['MERGED_HEADER','MERGED_BODY'],['tables','merged'])
    d=Document();d.add_paragraph('IMAGE_SENTINEL');d.add_picture(str(image),width=Inches(2));save('words_image',d,'docx',['IMAGE_SENTINEL'],['images'])
    d=Document();d.add_paragraph('BODY_SENTINEL');d.sections[0].header.paragraphs[0].text='HEADER_SENTINEL';d.sections[0].footer.paragraphs[0].text='FOOTER_SENTINEL';save('words_header_footer',d,'docx',['BODY_SENTINEL'],['headers','footers'])
    d=Document();p=d.add_paragraph();link=OxmlElement('w:hyperlink');rid=p.part.relate_to('https://example.org/test?x=1&y=2','http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink',is_external=True);link.set(qn('r:id'),rid);r=OxmlElement('w:r');t=OxmlElement('w:t');t.text='LINK_SENTINEL';r.append(t);link.append(r);p._p.append(link);save('words_hyperlink',d,'docx',['LINK_SENTINEL'],['links'])
    d=Document();d.add_paragraph('PAGE_ONE');d.add_page_break();d.add_paragraph('PAGE_TWO');save('words_page_break',d,'docx',['PAGE_ONE','PAGE_TWO'])
    d=Document();d.add_paragraph('PORTRAIT_SECTION');s=d.add_section(WD_SECTION_START.NEW_PAGE);s.orientation=WD_ORIENT.LANDSCAPE;s.page_width=Inches(11.7);s.page_height=Inches(8.3);d.add_paragraph('LANDSCAPE_SECTION');save('words_sections',d,'docx',['PORTRAIT_SECTION','LANDSCAPE_SECTION'])
    d=Document()
    for i in range(120):d.add_paragraph(f'LARGE_PARAGRAPH_{i}')
    save('words_large',d,'docx',['LARGE_PARAGRAPH_0','LARGE_PARAGRAPH_119'])
    w=Workbook();s=w.active;s.title='Data';s.append(['VALUE_SENTINEL',42,3.14159,True]);s.append(['café اردو 中文',-10,0,None]);save('sheets_values',w,'xlsx',checks=['cells'])
    w=Workbook();s=w.active;s.title='Data';s.append([2,3,'=SUM(A1:B1)']);s.append([4,5,'=A2*B2']);s['D1']='=IF(A1>0,"yes","no")';save('sheets_formulas',w,'xlsx',checks=['cells','formula_results'])
    w=Workbook();s=w.active;s['A1']='DATE_SENTINEL';s['B1']=datetime.datetime(2026,11,3);s['B1'].number_format='yyyy-mm-dd';s['C1']=.125;s['C1'].number_format='0.00%';s['D1']=1234.5;s['D1'].number_format='#,##0.00';save('sheets_number_formats',w,'xlsx',checks=['cells','number_formats'])
    w=Workbook();s=w.active;s['A1']='STYLE_SENTINEL';s['A1'].font=Font(name='Noto Sans',bold=True,color='FFFFFF');s['A1'].fill=PatternFill('solid',fgColor='2255AA');s['A1'].alignment=Alignment(horizontal='center');s.column_dimensions['A'].width=30;s.row_dimensions[1].height=28;save('sheets_styles',w,'xlsx',checks=['cells','styles'])
    w=Workbook();s=w.active;s.merge_cells('A1:C1');s['A1']='MERGED_SENTINEL';s['A2']=100;save('sheets_merged',w,'xlsx',checks=['cells','merged'])
    w=Workbook();s=w.active;s.title='First';s['A1']=5;s=w.create_sheet('Second');s['A1']="='First'!A1*2";s=w.create_sheet('Hidden');s.sheet_state='hidden';s['A1']='HIDDEN_SENTINEL';save('sheets_multiple_hidden',w,'xlsx',checks=['cells','formula_results','hidden'])
    w=Workbook();s=w.active;s['A1']='LINK_SENTINEL';s['A1'].hyperlink='https://example.org/sheets';s['A2']='Commented';from openpyxl.comments import Comment;s['A2'].comment=Comment('COMMENT_SENTINEL','Calligra');save('sheets_links_comments',w,'xlsx',checks=['cells','links','comments'])
    w=Workbook();s=w.active;s.append(['Header A','Header B']);s.append([1,2]);s.freeze_panes='B2';s.auto_filter.ref='A1:B2';s.print_area='A1:B2';save('sheets_view_print',w,'xlsx',checks=['cells','freeze','autofilter'])
    w=Workbook();s=w.active;s['A1']=10;w.defined_names.add(DefinedName('MY_AMOUNT',attr_text="'Sheet'!$A$1"));s['A2']='=MY_AMOUNT*2';save('sheets_named_range',w,'xlsx',checks=['cells','formula_results','names'])
    w=Workbook();s=w.active;s.title="Sales Q1. O'Brien";s['A1']=10;s['A2']=20;w.defined_names.add(DefinedName('SALES_RANGE',attr_text="'Sales Q1. O''Brien'!$A$1:$A$2"));save('sheets_named_range_quoted',w,'xlsx',checks=['cells','names'])
    w=Workbook();s=w.active;s['A1']='IMAGE_SENTINEL';s.add_image(SheetImage(str(image)),'C3');save('sheets_image',w,'xlsx',checks=['cells','images'])
    for kind,ctor in [('column',BarChart),('line',LineChart),('pie',PieChart),('scatter',ScatterChart)]:
        w=Workbook();s=w.active;s.append(['X','Y']);s.append([1,2]);s.append([2,5]);s.append([3,7]);chart=ctor()
        if kind=='scatter':chart.series.append(Series(Reference(s,min_col=2,min_row=2,max_row=4),Reference(s,min_col=1,min_row=2,max_row=4),title='Series'))
        else:chart.add_data(Reference(s,min_col=2,min_row=1,max_row=4),titles_from_data=True)
        s.add_chart(chart,'D2');save('sheets_chart_'+kind,w,'xlsx',checks=['cells','charts'])
    w=Workbook();s=w.active;s['A1']='INPUT_SENTINEL';v=DataValidation(type='list',formula1='"One,Two,Three"');s.add_data_validation(v);v.add(s['A2']);s.conditional_formatting.add('B1:B5',ColorScaleRule(start_type='min',start_color='FF0000',end_type='max',end_color='00FF00'));save('sheets_validation',w,'xlsx',checks=['cells','validation','conditional'])
    w=Workbook();s=w.active
    for i in range(1,501):s.append([f'ROW_{i}',i,i*2,f'=B{i}+C{i}'])
    save('sheets_large',w,'xlsx',checks=['cells','formula_results'])
    (root/'fixtures.json').write_text(json.dumps(cases,indent=2));return cases

def word_features(path):
    out=dict(text='',headers=[],footers=[],tables=0,merged=0,images=0,links=[],bold=0,italic=0,underline=0)
    with zipfile.ZipFile(path) as z:
        for name in z.namelist():
            if name.startswith('word/') and name.endswith('.xml'):
                r=E.fromstring(z.read(name));txt=''.join(r.xpath('//*[local-name()="t"]/text()'))
                if name=='word/document.xml':
                    out['text']=txt
                    for k,t in [('tables','tbl'),('merged','gridSpan'),('images','blip'),('bold','b'),('italic','i'),('underline','u')]:out[k]=len(r.xpath('//*[local-name()="'+t+'"]'))
                if name.startswith('word/header'):out['headers'].append(txt)
                if name.startswith('word/footer'):out['footers'].append(txt)
            if name.endswith('.rels'):
                for rel in E.fromstring(z.read(name)):
                    if rel.get('Type','').endswith('/hyperlink'):out['links'].append(rel.get('Target'))
    return out

def sheet_features(path,data_only=False):
    w=load_workbook(path,data_only=data_only);out={}
    for s in w:
        cells={c.coordinate:c.value for row in s for c in row if c.value is not None}
        out[s.title]=dict(cells=cells,hidden=s.sheet_state,merged=sorted(str(x) for x in s.merged_cells.ranges),freeze=s.freeze_panes,autofilter=s.auto_filter.ref,images=len(s._images),charts=len(s._charts),names={name:list(w.defined_names[name].destinations) for name in sorted(w.defined_names)},validation=len(s.data_validations.dataValidation),conditional=len(s.conditional_formatting),links={c.coordinate:c.hyperlink.target for row in s for c in row if c.hyperlink},comments={c.coordinate:c.comment.text for row in s for c in row if c.comment},number_formats={c.coordinate:c.number_format for row in s for c in row if c.value is not None},styles={c.coordinate:(c.font.bold,c.fill.fgColor.rgb if c.fill.fgColor.type=='rgb' else c.fill.fgColor.type) for row in s for c in row if c.value is not None})
    w.close();return out

def equal(a,b):
    # LibreOffice rewrites boolean constants to zero-argument formulas.
    # Compare their meaning while keeping booleans distinct from numbers.
    if isinstance(a,bool) or isinstance(b,bool):
        def boolean(v):
            if isinstance(v,bool):return v
            if isinstance(v,str) and v.upper() in ('=TRUE()','=FALSE()'):return v.upper()=='=TRUE()'
            return None
        return boolean(a) is not None and boolean(a)==boolean(b)
    if isinstance(a,(float,int)) and isinstance(b,(float,int)):return abs(a-b)<1e-7*max(1,abs(a))
    if isinstance(a,str) and isinstance(b,str) and a.startswith('=') and b.startswith('='):return a.replace("'",'').replace(' ','').upper()==b.replace("'",'').replace(' ','').upper()
    return a==b

def check(case,args,env):
    root=args.output/'cases'/case['name'];root.mkdir(parents=True,exist_ok=True)
    result=dict(name=case['name'],format=case['kind'],passed=False,checks={},issues=[])
    try:
        fmt=case['kind'];source=Path(case['path']);reference=root/('reference.'+fmt)
        run(['/usr/bin/python3',REFERENCE,source,reference,root/'reference.pdf'],env,root/'reference.log')
        run([args.build/'bin/office_roundtrip',fmt,source,root],env,root/'calligra.log',180)
        status=json.loads((root/'result.json').read_text());result['application']=status
        tests=result['checks'];tests['import_registered']=status['import_registered'];tests['export_registered']=status['export_registered'];tests['native_saved']=status['native_saved'];tests['office_saved']=status['office_saved']
        actual=root/('calligra.'+fmt)
        run(['/usr/bin/python3',REFERENCE,actual,root/('reopened.'+fmt),root/'reopened.pdf'],env,root/'reopen.log')
        tests['libreoffice_reopen']=True
        if fmt=='docx':
            before=word_features(reference);after=word_features(actual)
            tests['text']=all(t in after['text'] for t in case['tokens'])
            for key in case['checks']:
                if key in ['headers','footers']:
                    # OOXML keeps separate (possibly empty) parts for default,
                    # first-page and even-page headers/footers.  An empty part
                    # carries no content, so only non-empty text is compared.
                    tests[key]=set(x for x in before[key] if x)<=set(after[key])
                elif key=='links':tests[key]=set(before[key])<=set(after[key])
                else:tests[key]=after[key]>=before[key]
            result['reference_features']=before;result['export_features']=after
        else:
            before=sheet_features(reference);after=sheet_features(actual);tests['sheet_names']=list(before)==list(after)
            for key in case['checks']:
                if key=='formula_results':
                    expected=sheet_features(reference,True);observed=sheet_features(root/'reopened.xlsx',True)
                    tests[key]=all(name in observed and all(equal(v,observed[name]['cells'].get(c)) for c,v in sheet['cells'].items()) for name,sheet in expected.items())
                elif key in ['cells','number_formats','styles','links','comments','names']:
                    tests[key]=all(name in after and all(equal(v,after[name][key].get(c)) for c,v in sheet[key].items()) for name,sheet in before.items())
                else:tests[key]=all(name in after and sheet[key]==after[name][key] for name,sheet in before.items())
            result['reference_features']=before;result['export_features']=after
        result['issues']=[k for k,v in tests.items() if not v];result['passed']=not result['issues']
    except Exception as error:result['issues'].append(str(error))
    (root/'checks.json').write_text(json.dumps(result,indent=2,default=str))
    print(('PASS ' if result['passed'] else 'FAIL ')+case['name']+': '+', '.join(result['issues']),flush=True);return result

def negatives(args,env):
    root=args.output/'negative';root.mkdir(exist_ok=True);out=[];worker=args.build/'bin/calligraofficeconverter'
    for fmt,native in [('docx','odt'),('xlsx','ods')]:
        source=root/('broken.'+fmt);source.write_bytes(b'broken package');target=root/('keep.'+native);target.write_bytes(b'KEEP')
        p=subprocess.run([str(worker),'--input',str(source),'--output',str(target),'--from',fmt,'--to',native],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        out.append(dict(name='invalid_'+fmt,passed=p.returncode!=0 and target.read_bytes()==b'KEEP'))
    valid=args.output/'fixtures/words_empty.docx';target=root/'cross_family.ods';target.write_bytes(b'KEEP')
    p=subprocess.run([str(worker),'--input',str(valid),'--output',str(target),'--from','docx','--to','ods'],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    out.append(dict(name='cross_family_rejected',passed=p.returncode!=0 and target.read_bytes()==b'KEEP'));return out

def main():
    p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--install',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--jobs',type=int,default=2);p.add_argument('--cases',nargs='*');p.add_argument('--reuse-passing',action='store_true');args=p.parse_args()
    args.build=args.build.resolve();args.install=args.install.resolve();args.output=args.output.resolve();args.output.mkdir(parents=True,exist_ok=True)
    env=os.environ.copy();env.update(QT_QPA_PLATFORM='offscreen',QT_QPA_PLATFORMTHEME='generic',QT_LOGGING_RULES='calligra.*.debug=false;kf.*.debug=false',SAL_USE_VCLPLUGIN='svp',GSETTINGS_BACKEND='memory',XDG_CONFIG_HOME=str(args.output/'config'),QT_PLUGIN_PATH=str(args.install/'lib/x86_64-linux-gnu/qt6/plugins'),LD_LIBRARY_PATH=str(args.install/'lib/x86_64-linux-gnu'),XDG_DATA_DIRS=str(args.install/'share')+':/usr/local/share:/usr/share',PATH=str(args.install/'bin')+':'+env.get('PATH',''))
    manifest=args.output/'fixtures/fixtures.json';cases=json.loads(manifest.read_text()) if args.reuse_passing and manifest.exists() else fixtures(args.output/'fixtures')
    if args.cases:cases=[c for c in cases if c['name'] in args.cases]
    def evaluate(case):
        cached=args.output/'cases'/case['name']/'checks.json'
        if args.reuse_passing and cached.exists():
            r=json.loads(cached.read_text())
            if r['passed']:return r
        return check(case,args,env)
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:results=list(pool.map(evaluate,cases))
    errors=negatives(args,env)
    report=dict(reference='Installed LibreOfficeKit',cases=results,negative=errors,passed=sum(r['passed'] for r in results),total=len(results),limitations=['Finite structural and semantic checks; not exhaustive Office compatibility or exact visual layout certification.','The editable Calligra ODF models may lose features even when LibreOffice conversion preserves them.'])
    (args.output/'report.json').write_text(json.dumps(report,indent=2,default=str))
    lines=['# Words DOCX and Sheets XLSX compatibility','',f"{report['passed']}/{report['total']} cases passed all checks.",'','| Case | Result | Failed checks |','|---|---|---|']
    lines += [f"| {r['name']} | {'PASS' if r['passed'] else 'FAIL'} | {'; '.join(r['issues']).replace(chr(10),' ').replace('|','/')} |" for r in results]
    lines += ['','## Error handling','']+[f"- {r['name']}: {'PASS' if r['passed'] else 'FAIL'}" for r in errors]
    lines += ['','## Scope','']+['- '+x for x in report['limitations']]
    (args.output/'REPORT.md').write_text('\n'.join(lines)+'\n');print(f"SUMMARY {report['passed']}/{report['total']}: {args.output/'REPORT.md'}",flush=True)
    return 0 if all(r['passed'] for r in results+errors) else 1
if __name__=='__main__':sys.exit(main())
