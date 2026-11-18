#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Check DOCX table, page margin, contextual-spacing and TOC round trips."""
import argparse
import os
import re
import shutil
from pathlib import Path
import subprocess
import zipfile
from lxml import etree as E
from docx import Document
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Pt, Twips
from docx.enum.text import WD_BREAK, WD_TAB_ALIGNMENT, WD_TAB_LEADER

ROOT = Path(__file__).resolve().parents[5]
W = 'http://schemas.openxmlformats.org/wordprocessingml/2006/main'
NS = {'text': 'urn:oasis:names:tc:opendocument:xmlns:text:1.0',
      'style': 'urn:oasis:names:tc:opendocument:xmlns:style:1.0',
      'fo': 'urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0',
      'table': 'urn:oasis:names:tc:opendocument:xmlns:table:1.0',
      'calligra': 'http://www.calligra.org/2005/'}


def check(output):
    output.mkdir(parents=True, exist_ok=False)
    doc = Document()
    section = doc.sections[0]
    # Word puts the header/footer distance inside w:top/w:bottom.  Calligra's
    # ODF page layout stores them separately, so round trips used to return
    # them swapped (and divided by 20 twice), changing the text area.
    section.top_margin = Twips(720)
    section.bottom_margin = Twips(720)
    section.footer_distance = Twips(360)
    section.footer.paragraphs[0].text = 'Footer'
    doc.add_paragraph('Bullet one', 'List Bullet')
    doc.add_paragraph('Bullet two', 'List Bullet')
    p = doc.add_paragraph('Context disabled', 'List Bullet')
    flag = OxmlElement('w:contextualSpacing'); flag.set(qn('w:val'), '0')
    p._p.get_or_add_pPr().append(flag)
    table = doc.add_table(rows=1, cols=2)
    table.style = 'Table Grid'
    table.cell(0, 0).text = 'Table default'
    p = table.cell(0, 1).paragraphs[0]
    p.text = 'Table direct'
    p.paragraph_format.space_after = Pt(7)
    for tag, value in [('fldChar', 'begin'), ('instrText', ' TOC \\o "1-3" '), ('fldChar', 'separate')]:
        run = doc.add_paragraph().add_run()
        node = OxmlElement('w:' + tag)
        if tag == 'instrText': node.text = value
        else: node.set(qn('w:fldCharType'), value)
        run._r.append(node)
    p = doc.add_paragraph()
    p.paragraph_format.space_after = Pt(3)
    run = p.add_run('Entry\t1'); run.font.name = 'Arial'; run.font.size = Pt(11)
    # Word-generated tables of contents often keep the page-number tab in the
    # right margin (this template's text is 6" wide; the tab is at 7.6").
    # Word pulls the number back to the right indent, so the layout must keep
    # it inside the text area instead of painting it in the margin.
    p.paragraph_format.tab_stops.add_tab_stop(Twips(11000), WD_TAB_ALIGNMENT.RIGHT, WD_TAB_LEADER.DOTS)
    end = OxmlElement('w:fldChar'); end.set(qn('w:fldCharType'), 'end')
    doc.add_paragraph().add_run()._r.append(end)
    doc.add_heading('Entry', level=1)
    doc.add_paragraph('Trailing break').add_run().add_break(WD_BREAK.PAGE)
    doc.add_paragraph('After trailing break')
    p = doc.add_paragraph('Before inline break')
    p.add_run().add_break(WD_BREAK.PAGE)
    p.add_run('After inline break')
    doc.add_page_break()
    doc.add_paragraph('After standalone break')
    doc.add_page_break()
    doc.add_table(rows=1, cols=1).cell(0, 0).text = 'Table after break'

    # Word writes the default (equal) grid even when the cells carry the real
    # widths, so a reader that trusts w:gridCol alone makes every column the
    # same width.  The first table fits its grid exactly (1:5), the second is
    # wider than the grid and has to be scaled back (1:4).
    def width_table(left, right, left_twips, right_twips):
        table = doc.add_table(rows=1, cols=2)
        table.style = 'Table Grid'
        table.cell(0, 0).text = left
        table.cell(0, 1).text = right
        for cell, twips in zip(table.rows[0].cells, (left_twips, right_twips)):
            properties = cell._tc.get_or_add_tcPr()
            for old in properties.findall(qn('w:tcW')):
                properties.remove(old)
            width = OxmlElement('w:tcW')
            width.set(qn('w:type'), 'dxa')
            width.set(qn('w:w'), str(twips))
            properties.append(width)
    width_table('Width fits', 'Width fits right', 1440, 7200)
    width_table('Width scaled', 'Width scaled right', 2880, 11520)

    # A run that overrides only its size (as table cells typically do) must
    # still carry the document's default font.  Without it Calligra rendered
    # the cell with Noto Sans instead of the theme's Cambria and shifted
    # pagination by a page.
    sized = doc.add_paragraph().add_run('Sized only')
    sized.font.size = Pt(9)
    missing = doc.add_paragraph().add_run('Missing face')
    missing.font.name = 'ZZZUninstalledTestFont'

    source = output / 'input.docx'; target = output / 'imported.odt'
    doc.save(source)
    install = ROOT / 'build/install'
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_LOGGING_RULES='*.debug=false',
               LD_LIBRARY_PATH=str(ROOT / 'build/bin'),
               QT_PLUGIN_PATH=str(install / 'lib/x86_64-linux-gnu/qt6/plugins'),
               XDG_DATA_DIRS=str(install / 'share') + ':/usr/local/share:/usr/share')
    subprocess.run([ROOT / 'build/bin/calligraconverter', '--batch', source, target], env=env, check=True, timeout=60)
    with zipfile.ZipFile(target) as package:
        roots = [E.fromstring(package.read(name)) for name in ['styles.xml', 'content.xml']]
    styles = {s.get(qn_odf('style:name')): s for r in roots for s in r.xpath('//style:style', namespaces=NS)}

    def properties(name):
        style = styles.get(name)
        if style is None: return {}
        props = properties(style.get(qn_odf('style:parent-style-name')))
        for child in style:
            props.update(child.attrib)
        return props

    def paragraph(text):
        return next(p for p in roots[1].xpath('//text:p', namespaces=NS) if ''.join(p.itertext()) == text)

    assert properties(paragraph('Bullet one').get(qn_odf('text:style-name')))[qn_odf('calligra:contextual-spacing')] == 'true'
    assert properties(paragraph('Context disabled').get(qn_odf('text:style-name')))[qn_odf('calligra:contextual-spacing')] == 'false'
    for text, spacing in [('Table default', 0), ('Table direct', 7)]:
        props = properties(paragraph(text).get(qn_odf('text:style-name')))
        assert props[qn_odf('fo:line-height')] == '100%'
        assert float(props[qn_odf('fo:margin-bottom')][:-2]) == spacing
    template = roots[1].xpath('//text:table-of-content-entry-template', namespaces=NS)[0]
    props = properties(template.get(qn_odf('text:style-name')))
    assert float(props[qn_odf('fo:margin-bottom')][:-2]) == 3
    assert float(props[qn_odf('fo:font-size')][:-2]) == 11
    trailing = paragraph('Trailing break')
    assert properties(trailing.get(qn_odf('text:style-name')))[qn_odf('fo:break-after')] == 'page'
    assert ''.join(trailing.getnext().itertext()) == 'After trailing break'
    assert properties(paragraph('After inline break').get(qn_odf('text:style-name')))[qn_odf('fo:break-before')] == 'page'
    standalone = paragraph('After standalone break')
    assert properties(standalone.get(qn_odf('text:style-name')))[qn_odf('fo:break-before')] == 'page'
    assert ''.join(standalone.getprevious().itertext()) == 'After inline break'
    table = paragraph('Table after break').getparent().getparent().getparent()
    table_ns = 'urn:oasis:names:tc:opendocument:xmlns:table:1.0'
    assert properties(table.get('{' + table_ns + '}style-name'))[qn_odf('fo:break-before')] == 'page'

    def column_widths(text):
        cell = paragraph(text)
        table_el = next(a for a in cell.iterancestors() if a.tag == qn_odf('table:table'))
        widths = []
        for column in table_el.findall(qn_odf('table:table-column')):
            style = styles[column.get(qn_odf('table:style-name'))]
            props = style.find(qn_odf('style:table-column-properties'))
            widths.append(round(float(props.get(qn_odf('style:column-width'))[:-2]), 2))
        return widths

    assert column_widths('Width fits') == [72.0, 360.0]
    # 2880+11520 twips is wider than the 8640-twip grid, so Word would shrink
    # the table to the text area; the 1:4 proportion has to survive that.
    assert column_widths('Width scaled') == [86.4, 345.6]

    # A style that overrides only the size must still name the default face:
    # otherwise Calligra substitutes Noto Sans and pagination changes.
    sized_style = paragraph('Sized only').find(qn_odf('text:span')).get(qn_odf('text:style-name'))
    assert properties(sized_style).get(qn_odf('style:font-name')) == 'Cambria'
    # An uninstalled font must declare the generic family so that the
    # substitute (and therefore its line height) matches other applications.
    missing_style = paragraph('Missing face').find(qn_odf('text:span')).get(qn_odf('text:style-name'))
    missing_props = properties(missing_style)
    assert missing_props.get(qn_odf('style:font-name')) == 'ZZZUninstalledTestFont'
    assert missing_props.get(qn_odf('style:font-family-generic')) == 'roman'

    def page_heights(root):
        props = root.xpath('//style:page-layout/style:page-layout-properties', namespaces=NS)[0]
        footer = root.xpath('//style:page-layout/style:footer-style/style:header-footer-properties', namespaces=NS)[0]
        return (float(props.get(qn_odf('fo:margin-top'))[:-2]),
                float(props.get(qn_odf('fo:margin-bottom'))[:-2]),
                float(footer.get(qn_odf('fo:min-height'))[:-2]))

    # w:top/w:bottom (720 twips) include the header/footer distance (360);
    # Calligra splits them into the text margin and the footer's own height.
    expected_margins = (36.0, 18.0, 18.0)
    assert page_heights(roots[0]) == expected_margins

    exported = output / 'exported.docx'
    subprocess.run([str(ROOT / 'build/bin/calligraconverter'), '--batch', str(target), str(exported)],
                   env=env, check=True, timeout=60)
    wn = {'w': W}
    with zipfile.ZipFile(exported) as package:
        exported_document = E.fromstring(package.read('word/document.xml'))
        exported_styles = E.fromstring(package.read('word/styles.xml'))
    margins = exported_document.xpath('//w:pgMar', namespaces=wn)[0]
    assert (margins.get('{%s}top' % W), margins.get('{%s}bottom' % W), margins.get('{%s}footer' % W)) \
        == ('720', '720', '360')

    def exported_grid(text):
        table_el = exported_document.xpath('//w:tbl[.//w:t[text()="%s"]]' % text, namespaces=wn)[0]
        return [c.get('{%s}w' % W) for c in table_el.findall('{%s}tblGrid/{%s}gridCol' % (W, W))]

    assert exported_grid('Width fits') == ['1440', '7200']
    assert exported_grid('Width scaled') == ['1728', '6912']
    bullet = exported_styles.xpath('//w:style[@w:styleId="ListBullet"]', namespaces=wn)[0]
    assert bullet.find('.//{%s}contextualSpacing' % W) is not None

    # Re-importing the export must not move the margins again.
    reimported = output / 'reimported.odt'
    subprocess.run([str(ROOT / 'build/bin/calligraconverter'), '--batch', str(exported), str(reimported)],
                   env=env, check=True, timeout=60)
    with zipfile.ZipFile(reimported) as package:
        reimported_styles = E.fromstring(package.read('styles.xml'))
    assert page_heights(reimported_styles) == expected_margins

    # The generated TOC entry keeps Word's out-of-margin page-number tab.  Word
    # pulls the number back to the right indent, so it may not be painted in
    # the margin.
    if shutil.which('pdftotext'):
        pdf = output / 'imported.pdf'
        subprocess.run([str(ROOT / 'build/bin/calligraconverter'), '--batch', str(target), str(pdf)],
                       env=env, check=True, timeout=60)
        boxes = output / 'page.html'
        subprocess.run(['pdftotext', '-f', '1', '-l', '1', '-bbox-layout', str(pdf), str(boxes)],
                       check=True, timeout=60)
        markup = boxes.read_text(errors='replace')
        entry = next(m for m in re.finditer(r'<line[^>]*xMax="([\d.]+)"[^>]*>(.*?)</line>', markup, re.S)
                     if 'Entry' in m.group(2))
        layout = roots[0].xpath('//style:page-layout/style:page-layout-properties', namespaces=NS)[0]
        right_edge = points(layout.get(qn_odf('fo:page-width'))) - points(layout.get(qn_odf('fo:margin-right')))
        assert float(entry.group(1)) <= right_edge + 0.5, \
            'TOC page number painted at %s, past the %s pt right margin' % (entry.group(1), right_edge)

    print('PASS: contextual spacing, table defaults/direct overrides, cell widths/grid, '
          'page margins, TOC entry formatting, hard breaks')


def qn_odf(name):
    prefix, local = name.split(':')
    return '{' + NS[prefix] + '}' + local


def points(value):
    number, unit = float(value[:-2]), value[-2:]
    return number * {'pt': 1.0, 'cm': 72.0 / 2.54, 'in': 72.0, 'mm': 72.0 / 25.4}[unit]


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    check(parser.parse_args().output.resolve())
