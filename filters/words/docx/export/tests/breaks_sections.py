#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Exercise authored page/column breaks and next-page/continuous DOCX sections."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import zipfile
from lxml import etree as E
from docx import Document
from docx.enum.section import WD_SECTION_START
from docx.enum.text import WD_BREAK
from docx.enum.style import WD_STYLE_TYPE
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
import fitz
from export_options import ROOT, REFERENCE, environment

W = {'w': 'http://schemas.openxmlformats.org/wordprocessingml/2006/main'}


def fixtures(folder):
    cases = []
    for name in ['paragraph_page', 'inline_page', 'column', 'next_page', 'continuous', 'same_format_sections', 'odd_page', 'even_page', 'mid_page', 'inherited_override', 'toc', 'empty_font_table', 'paragraph_border', 'paragraph_shading']:
        doc = Document()
        doc.add_paragraph('BEFORE_SENTINEL')
        if name == 'paragraph_page':
            doc.add_paragraph('AFTER_SENTINEL').paragraph_format.page_break_before = True
        elif name == 'paragraph_border':
            borders = OxmlElement('w:pBdr'); bottom = OxmlElement('w:bottom')
            for key, value in [('val','single'), ('sz','12'), ('space','18'), ('color','1D1F6E')]: bottom.set(qn('w:'+key), value)
            borders.append(bottom); doc.paragraphs[0]._p.get_or_add_pPr().append(borders)
            doc.add_paragraph('AFTER_SENTINEL')
        elif name == 'paragraph_shading':
            shading = OxmlElement('w:shd'); shading.set(qn('w:fill'), 'DDEEFF')
            doc.paragraphs[0]._p.get_or_add_pPr().append(shading)
            doc.add_paragraph('AFTER_SENTINEL')
        elif name == 'empty_font_table':
            doc.styles['Normal'].font.name = 'Times New Roman'
            doc.add_paragraph('AFTER_SENTINEL')
        elif name == 'inherited_override':
            style = doc.styles.add_style('BreakBeforeStyle', WD_STYLE_TYPE.PARAGRAPH)
            style.paragraph_format.page_break_before = True
            paragraph = doc.add_paragraph('AFTER_SENTINEL', style)
            paragraph.paragraph_format.page_break_before = False
        elif name == 'toc':
            paragraph = doc.add_paragraph()
            for kind in ['begin', 'separate', 'end']:
                if kind == 'separate':
                    run = paragraph.add_run(); instruction = OxmlElement('w:instrText'); instruction.text = ' TOC \\o "1-2" \\h '; run._r.append(instruction)
                if kind == 'end': paragraph.add_run('CACHED_TOC')
                run = paragraph.add_run(); marker = OxmlElement('w:fldChar'); marker.set(qn('w:fldCharType'), kind); run._r.append(marker)
            doc.add_heading('AFTER_SENTINEL', 1)
        elif name in ['inline_page', 'column', 'mid_page']:
            doc.paragraphs[-1].add_run().add_break(WD_BREAK.COLUMN if name == 'column' else WD_BREAK.PAGE)
            if name == 'mid_page': doc.paragraphs[-1].add_run('AFTER_SENTINEL')
            else: doc.add_paragraph('AFTER_SENTINEL')
            if name == 'column':
                doc.sections[0]._sectPr.find(qn('w:cols')).set(qn('w:num'), '2')
        else:
            section = doc.add_section({'continuous': WD_SECTION_START.CONTINUOUS, 'odd_page': WD_SECTION_START.ODD_PAGE, 'even_page': WD_SECTION_START.EVEN_PAGE}.get(name, WD_SECTION_START.NEW_PAGE))
            if name == 'next_page':
                section.footer.is_linked_to_previous = False
                section.footer.paragraphs[0].text = 'FOOTER_SENTINEL '
                run = section.footer.paragraphs[0].add_run()
                for kind in ['begin', 'separate', 'end']:
                    if kind == 'separate':
                        instruction = OxmlElement('w:instrText'); instruction.text = ' PAGE '; run._r.append(instruction)
                    marker = OxmlElement('w:fldChar'); marker.set(qn('w:fldCharType'), kind); run._r.append(marker)
                page = OxmlElement('w:pgNumType'); page.set(qn('w:fmt'), 'lowerRoman'); page.set(qn('w:start'), '1'); section._sectPr.append(page)
            doc.add_paragraph('AFTER_SENTINEL')
        source = folder / (name + '.docx'); doc.save(source)
        if name == 'empty_font_table':
            with zipfile.ZipFile(source) as package: contents = {n: package.read(n) for n in package.namelist()}
            contents['word/fontTable.xml'] = b'<w:fonts xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"/>'
            with zipfile.ZipFile(source, 'w', zipfile.ZIP_DEFLATED) as package:
                for n, data in contents.items(): package.writestr(n, data)
        cases.append((name, source))
    return cases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--plugin-path', type=Path)
    parser.add_argument('--cases', nargs='*')
    args = parser.parse_args(); args.output.mkdir(parents=True, exist_ok=False)
    env = environment()
    if args.plugin_path: env['QT_PLUGIN_PATH'] = str(args.plugin_path.resolve())
    results = []
    def run(command, log):
        with log.open('wb') as stream:
            subprocess.run(list(map(str, command)), env=env, stdout=stream, stderr=subprocess.STDOUT, check=True, timeout=120)
    for name, source in fixtures(args.output):
        if args.cases and name not in args.cases: continue
        odt = source.with_suffix('.odt'); target = args.output / (name + '-roundtrip.docx')
        run([ROOT/'build/bin/calligraconverter', '--batch', source, odt], args.output/(name+'-import.log'))
        run([ROOT/'build/bin/calligraconverter', '--batch', odt, target], args.output/(name+'-export.log'))
        with zipfile.ZipFile(source) as package: before = E.fromstring(package.read('word/document.xml'))
        with zipfile.ZipFile(target) as package:
            after = E.fromstring(package.read('word/document.xml'))
            valid = package.testzip() is None
            for part in package.namelist():
                if part.endswith(('.xml', '.rels')): E.fromstring(package.read(part))
        checks = {'valid_package': valid,
                  'body_text': ''.join(before.xpath('//w:t/text()', namespaces=W)) == ''.join(after.xpath('//w:t/text()', namespaces=W)),
                  'sections': len(before.xpath('//w:sectPr', namespaces=W)) == len(after.xpath('//w:sectPr', namespaces=W))}
        def body_geometry(xml):
            return [[int(s.find('w:'+tag, W).get(qn('w:'+attr))) for tag,attr in [('pgSz','w'),('pgSz','h'),('pgMar','top'),('pgMar','bottom'),('pgMar','left'),('pgMar','right')]] for s in xml.xpath('//w:sectPr', namespaces=W)]
        checks['page_geometry'] = body_geometry(before) == body_geometry(after)
        if name == 'paragraph_border': checks['border_padding'] = bool(after.xpath('//w:pBdr/w:bottom[@w:space="18"][@w:sz="12"]', namespaces=W))
        if name == 'paragraph_shading': checks['shading'] = any(value.upper() == 'DDEEFF' for value in after.xpath('//w:shd/@w:fill', namespaces=W))
        if name == 'next_page':
            checks['footer'] = bool(after.xpath('//w:footerReference', namespaces=W))
            checks['number_restart'] = bool(after.xpath('//w:pgNumType[@w:fmt="lowerRoman"][@w:start="1"]', namespaces=W))
            with zipfile.ZipFile(target) as package:
                checks['page_field'] = any(b'PAGE' in package.read(n) for n in package.namelist() if n.startswith('word/footer'))
        if name == 'empty_font_table':
            with zipfile.ZipFile(odt) as package:
                checks['font_declaration'] = b'Times New Roman' in package.read('styles.xml') and b'style:font-face' in package.read('styles.xml')
        if name == 'toc': checks['toc_field'] = bool(after.xpath('//w:instrText[contains(text(), \"TOC\")]', namespaces=W))
        if name == 'column': checks['column_break'] = bool(after.xpath('//w:br[@w:type="column"]', namespaces=W))
        for label, doc in [('before', source), ('after', target)]:
            pdf = args.output/(name+'-'+label+'.pdf')
            run(['/usr/bin/python3', REFERENCE, doc, pdf], args.output/(name+'-'+label+'-render.log'))
        pdf_before = fitz.open(args.output/(name+'-before.pdf')); pdf_after = fitz.open(args.output/(name+'-after.pdf'))
        checks['page_count'] = len(pdf_before) == len(pdf_after)
        def locations(pdf):
            return {token: [i+1 for i,p in enumerate(pdf) if token in p.get_text()] for token in ['BEFORE_SENTINEL','AFTER_SENTINEL']}
        checks['sentinel_pages'] = locations(pdf_before) == locations(pdf_after)
        results.append(dict(name=name, checks=checks, passed=all(checks.values()), before_pages=len(pdf_before), after_pages=len(pdf_after)))
        print(name, 'PASS' if all(checks.values()) else 'FAIL', checks, flush=True)
    (args.output/'report.json').write_text(json.dumps(results, indent=2)+'\n')
    return 0 if all(r['passed'] for r in results) else 1

if __name__ == '__main__': raise SystemExit(main())
