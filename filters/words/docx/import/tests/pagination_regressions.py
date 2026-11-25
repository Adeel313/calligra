#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Check DOCX table, page margin, contextual-spacing and TOC round trips."""
import argparse
from base64 import b64decode
import io
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
from docx.shared import Inches, Pt, Twips
from docx.enum.style import WD_STYLE_TYPE
from docx.enum.text import WD_BREAK, WD_TAB_ALIGNMENT, WD_TAB_LEADER

ROOT = Path(__file__).resolve().parents[5]
W = 'http://schemas.openxmlformats.org/wordprocessingml/2006/main'
NS = {'text': 'urn:oasis:names:tc:opendocument:xmlns:text:1.0',
      'style': 'urn:oasis:names:tc:opendocument:xmlns:style:1.0',
      'fo': 'urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0',
      'table': 'urn:oasis:names:tc:opendocument:xmlns:table:1.0',
      'draw': 'urn:oasis:names:tc:opendocument:xmlns:drawing:1.0',
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

    # Word indents a table from the text margin with w:tblInd. Without it the
    # table is laid out flush with the page's left text edge.
    indented = doc.add_table(rows=1, cols=1)
    indented.cell(0, 0).text = 'Indented table'
    indent = OxmlElement('w:tblInd')
    indent.set(qn('w:w'), '1134')
    indent.set(qn('w:type'), 'dxa')
    indented._tbl.tblPr.append(indent)

    # Word anchors a floating table to the page with w:tblpPr.  tblpX is
    # measured from the text margin, tblpY from the top of the page; the first
    # cell's left margin moves the table border further left.  Calligra used to
    # drop the anchor entirely and leave the table at the text margin.
    page_anchored = doc.add_table(rows=1, cols=1)
    page_anchored.style = 'Table Grid'
    page_anchored.cell(0, 0).text = 'Page anchored table'
    tblpr = page_anchored._tbl.tblPr
    tblp = OxmlElement('w:tblpPr')
    for name, value in [('leftFromText', '180'), ('rightFromText', '180'),
                        ('vertAnchor', 'page'), ('horzAnchor', 'margin'),
                        ('tblpX', '-612'), ('tblpY', '901')]:
        tblp.set(qn('w:' + name), value)
    tblpr.insert(0, tblp)
    cell_mar = OxmlElement('w:tblCellMar')
    left = OxmlElement('w:left')
    left.set(qn('w:w'), '108')
    left.set(qn('w:type'), 'dxa')
    cell_mar.append(left)
    tblpr.insert(1, cell_mar)

    # Word repeats rows carrying w:tblHeader when a table splits over pages.
    # ODF expresses that with table:table-header-rows, which must survive the
    # DOCX -> ODT -> DOCX round trip.
    header_table = doc.add_table(rows=3, cols=1)
    header_table.style = 'Table Grid'
    header_table.cell(0, 0).text = 'Repeated header'
    header_table.cell(1, 0).text = 'Body one'
    header_table.cell(2, 0).text = 'Body two'
    for row, height in zip(header_table.rows, (1200, 675, 675)):
        tr_pr = row._tr.get_or_add_trPr()
        tr_height = OxmlElement('w:trHeight')
        tr_height.set(qn('w:val'), str(height))
        tr_pr.append(tr_height)
    header_tr_pr = header_table.rows[0]._tr.get_or_add_trPr()
    header_tr_pr.append(OxmlElement('w:tblHeader'))

    # Named paragraph styles carry tab stops that direct paragraph tabs may
    # clear or override.  Exporting them is necessary for footer alignment.
    named_tabs = doc.styles.add_style('Named tabs', WD_STYLE_TYPE.PARAGRAPH)
    named_tpr = named_tabs.element.get_or_add_pPr()
    named_tab_list = OxmlElement('w:tabs')
    named_tab = OxmlElement('w:tab')
    named_tab.set(qn('w:val'), 'center')
    named_tab.set(qn('w:pos'), '4800')
    named_tab_list.append(named_tab)
    named_tab = OxmlElement('w:tab')
    named_tab.set(qn('w:val'), 'right')
    named_tab.set(qn('w:pos'), '9600')
    named_tab_list.append(named_tab)
    named_tpr.append(named_tab_list)
    doc.add_paragraph('Named tab paragraph', style='Named tabs')
    overridden = doc.add_paragraph('Named tab override', style='Named tabs')
    tabs = OxmlElement('w:tabs')
    for value, position in [('clear', '9600'), ('right', '9200')]:
        tab = OxmlElement('w:tab')
        tab.set(qn('w:val'), value)
        tab.set(qn('w:pos'), position)
        tabs.append(tab)
    overridden._p.get_or_add_pPr().append(tabs)

    # Word disables standard ligatures for ordinary text; PDF text extraction
    # must still see the original letters instead of ligature glyphs.
    ligature = doc.add_paragraph().add_run(
        'Di Martino Motivazione firmata attribuito attività')
    ligature.font.name = 'Calibri'

    # w:pBdr/@w:space is measured in points, not twips.  Shrinking it by 20
    # collapses callout insets and heading underline gaps, which reflows text.
    bordered = doc.add_paragraph('Border space in points')
    pbdr = OxmlElement('w:pBdr')
    for side, space in [('left', '8'), ('bottom', '6')]:
        border = OxmlElement('w:' + side)
        border.set(qn('w:val'), 'single')
        border.set(qn('w:sz'), '12')
        border.set(qn('w:space'), space)
        border.set(qn('w:color'), 'FF9900')
        pbdr.append(border)
    bordered._p.get_or_add_pPr().append(pbdr)
    ind = OxmlElement('w:ind')
    ind.set(qn('w:left'), '200')
    bordered._p.get_or_add_pPr().append(ind)

    # A floating object whose horizontal base is the page margin and whose
    # vertical base is its paragraph travels with that paragraph in Word.
    anchored = doc.add_paragraph('Anchored image follows')
    anchored.add_run().add_picture(io.BytesIO(b64decode(
        'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGP4//8/AAX+Av4N70a4AAAAAElFTkSuQmCC')),
        width=Inches(1))

    # A run that overrides only its size (as table cells typically do) must
    # still carry the document's default font.  Without it Calligra rendered
    # the cell with Noto Sans instead of the theme's Cambria and shifted
    # pagination by a page.
    sized = doc.add_paragraph().add_run('Sized only')
    sized.font.size = Pt(9)
    missing = doc.add_paragraph().add_run('Missing face')
    missing.font.name = 'ZZZUninstalledTestFont'
    mono = doc.add_paragraph().add_run('Mono face')
    mono.font.name = 'Consolas'

    # Word reserves vertical space with an empty paragraph whose paragraph-mark
    # run properties carry a large font size.  Dropping it collapses the line
    # and shifts everything below it upwards (cover pages use this heavily).
    spacer = doc.add_paragraph()
    mark_rpr = OxmlElement('w:rPr')
    mark_sz = OxmlElement('w:sz'); mark_sz.set(qn('w:val'), '460')
    mark_rpr.append(mark_sz)
    spacer._p.get_or_add_pPr().append(mark_rpr)
    # Cover pages put a tiny (3pt) run carrying the anchored background image
    # in this paragraph; its size must not override the paragraph mark's 230pt.
    spacer.add_run().font.size = Pt(3)
    doc.add_paragraph('After tall spacer')

    source = output / 'input.docx'; target = output / 'imported.odt'
    doc.save(source)

    # Turn the generated inline picture into the margin-anchored floating
    # object Word writes for this layout (w:positionH/@relativeFrom="margin",
    # w:positionV/@relativeFrom="paragraph").
    with zipfile.ZipFile(source) as package:
        entries = {name: package.read(name) for name in package.namelist()}
    document = E.fromstring(entries['word/document.xml'])
    inline = document.find('.//' + qn('wp:inline'))
    assert inline is not None, 'fixture has no inline drawing'
    anchor = E.Element(qn('wp:anchor'), dict(inline.attrib))
    for name, value in [('distT', '0'), ('distB', '0'), ('distL', '0'), ('distR', '0'),
                        ('simplePos', '0'), ('relativeHeight', '251658240'),
                        ('behindDoc', '0'), ('locked', '0'),
                        ('layoutInCell', '1'), ('allowOverlap', '1')]:
        anchor.set(name, value)
    simple = E.SubElement(anchor, qn('wp:simplePos')); simple.set('x', '0'); simple.set('y', '0')
    horizontal = E.SubElement(anchor, qn('wp:positionH')); horizontal.set('relativeFrom', 'margin')
    E.SubElement(horizontal, qn('wp:posOffset')).text = '733425'
    vertical = E.SubElement(anchor, qn('wp:positionV')); vertical.set('relativeFrom', 'paragraph')
    E.SubElement(vertical, qn('wp:posOffset')).text = '0'
    for child in list(inline):
        anchor.append(child)
    inline.getparent().replace(inline, anchor)
    entries['word/document.xml'] = E.tostring(document)
    with zipfile.ZipFile(source, 'w') as package:
        for name, data in entries.items():
            package.writestr(name, data)

    install = Path(os.environ.get('CALLIGRA_DATA_ROOT', ROOT / 'build/install'))
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

    assert properties(paragraph('Bullet one').get(qn_odf('text:style-name')))[qn_odf('style:contextual-spacing')] == 'true'
    assert properties(paragraph('Context disabled').get(qn_odf('text:style-name')))[qn_odf('style:contextual-spacing')] == 'false'

    border_props = properties(paragraph('Border space in points').get(qn_odf('text:style-name')))
    assert points(border_props[qn_odf('fo:padding-left')]) == 8.0
    assert points(border_props[qn_odf('fo:padding-bottom')]) == 6.0
    # 1.5pt border + 8pt space is carved out of the 10pt OOXML indent.
    assert points(border_props[qn_odf('fo:margin-left')]) == 0.5

    # A zero list-tab position makes other renderers advance to the next default
    # tab stop instead of aligning the text at fo:margin-left.
    for root in roots:
        for alignment in root.xpath('//style:list-level-label-alignment', namespaces=NS):
            tab = alignment.get(qn_odf('text:list-tab-stop-position'))
            if tab is not None:
                assert points(tab) > 0, 'zero list-tab-stop-position was exported'

    for text, spacing in [('Table default', 0), ('Table direct', 7)]:
        props = properties(paragraph(text).get(qn_odf('text:style-name')))
        assert props[qn_odf('fo:line-height')] == '100%'
        assert float(props[qn_odf('fo:margin-bottom')][:-2]) == spacing
    # Table Grid inherits its cell margins from Table Normal (108 twips).
    cell_style = paragraph('Table default').getparent().get(qn_odf('table:style-name'))
    assert points(properties(cell_style)[qn_odf('fo:padding-left')]) == 5.4
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

    indented_table = paragraph('Indented table').getparent().getparent().getparent()
    indent_props = properties(indented_table.get(qn_odf('table:style-name')))
    assert round(points(indent_props[qn_odf('fo:margin-left')]), 1) == 56.7

    page_anchored_table = paragraph('Page anchored table').getparent().getparent().getparent()
    page_props = properties(page_anchored_table.get(qn_odf('table:style-name')))
    assert page_props.get(qn_odf('calligra:table-page-anchored')) == 'true'
    # -612 twips from the margin minus the 108-twip first-cell margin = -36pt.
    assert round(points(page_props[qn_odf('calligra:table-offset-x')]), 1) == -36.0
    # 901 twips from the page top minus Word's one-twip adjustment and the
    # 36pt section top margin = 9pt below the top of the Calligra text area.
    assert round(points(page_props[qn_odf('calligra:table-offset-y')]), 1) == 9.0

    def ancestor_table(text):
        return next(a for a in paragraph(text).iterancestors() if a.tag == qn_odf('table:table'))

    header_rows = ancestor_table('Repeated header').find(qn_odf('table:table-header-rows'))
    assert header_rows is not None, 'w:tblHeader did not become table:table-header-rows'
    assert len(header_rows.findall(qn_odf('table:table-row'))) == 1
    assert ''.join(header_rows.itertext()).strip() == 'Repeated header'
    header_style = header_rows.find(qn_odf('table:table-row')).get(qn_odf('table:style-name'))
    assert points(properties(header_style)[qn_odf('style:min-row-height')]) == 60.0

    def direct_tab_stops(text):
        style = styles[paragraph(text).get(qn_odf('text:style-name'))]
        stops = style.find('.//' + qn_odf('style:tab-stops'))
        if stops is None:
            return []
        return [(stop.get(qn_odf('style:type')),
                 round(points(stop.get(qn_odf('style:position'))), 2))
                for stop in stops.findall(qn_odf('style:tab-stop'))]

    # w:tabs is additive; clearing the style's right tab must leave its center
    # tab in the automatic style instead of replacing the whole list.
    actual_tabs = direct_tab_stops('Named tab override')
    assert actual_tabs == [('right', 460.0), ('center', 240.0)], actual_tabs

    # A margin-horizontal/paragraph-vertical base is char-anchored in ODF.
    # A paragraph anchor makes Calligra put the object at the top of the page.
    anchored_frame = next(frame for frame in roots[1].xpath('//draw:frame', namespaces=NS)
                          if frame.get(qn_odf('text:anchor-type')) == 'char')
    frame_props = properties(anchored_frame.get(qn_odf('draw:style-name')))
    assert frame_props.get(qn_odf('style:horizontal-rel')) == 'page-content'
    assert frame_props.get(qn_odf('style:vertical-rel')) == 'paragraph'

    # A style that overrides only the size must still name the default face:
    # otherwise Calligra substitutes Noto Sans and pagination changes.
    sized_style = paragraph('Sized only').find(qn_odf('text:span')).get(qn_odf('text:style-name'))
    assert properties(sized_style).get(qn_odf('style:font-name')) == 'Cambria'
    # An uninstalled font without a declared generic family must not be
    # forced to serif: the font matcher should choose its substitute.
    missing_style = paragraph('Missing face').find(qn_odf('text:span')).get(qn_odf('text:style-name'))
    missing_props = properties(missing_style)
    assert missing_props.get(qn_odf('style:font-name')) == 'ZZZUninstalledTestFont'
    assert missing_props.get(qn_odf('style:font-family-generic')) is None
    # Word's monospace UI face is absent on Linux; Word-compatible renderers
    # substitute Arial.  Keeping the name would let Qt pick a local monospace
    # face with different run widths and shift line breaks.
    mono_style = paragraph('Mono face').find(qn_odf('text:span')).get(qn_odf('text:style-name'))
    assert properties(mono_style).get(qn_odf('style:font-name')) == 'Arial'

    # The empty spacer's 230pt paragraph-mark size must reach the ODF paragraph
    # style; otherwise the line has normal height and following text moves up.
    spacer_style = paragraph('After tall spacer').getprevious().get(qn_odf('text:style-name'))
    assert points(properties(spacer_style)[qn_odf('fo:font-size')]) == 230.0

    def page_heights(root):
        props = root.xpath('//style:page-layout/style:page-layout-properties', namespaces=NS)[0]
        footer = root.xpath('//style:page-layout/style:footer-style/style:header-footer-properties', namespaces=NS)[0]
        return (float(props.get(qn_odf('fo:margin-top'))[:-2]),
                float(props.get(qn_odf('fo:margin-bottom'))[:-2]),
                points(footer.get(qn_odf('fo:min-height'))) if footer.get(qn_odf('fo:min-height')) else 0.0,
                points(footer.get(qn_odf('fo:margin-top'))) if footer.get(qn_odf('fo:margin-top')) else 0.0,
                footer.get(qn_odf('style:dynamic-spacing')))

    # w:bottom (720 twips) is the distance to the body text; the footer sits
    # w:footer (360) from the paper edge.  Calligra uses the latter as the page
    # margin and discounts the footer frame's one-line height from the gap, so
    # the body foot lands at the Word position instead of one line too early.
    expected_margins = (36.0, 18.0, 0.0, 3.4, 'false')
    assert page_heights(roots[0]) == expected_margins
    layout_props = roots[0].xpath('//style:page-layout/style:page-layout-properties', namespaces=NS)[0]
    assert points(layout_props.get('{http://www.calligra.org/2005/}docx-body-bottom-margin')) == 36.0

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
    exported_header_table = exported_document.xpath('//w:tbl[.//w:t[text()="Repeated header"]]', namespaces=wn)[0]
    exported_header_row = exported_header_table.xpath('.//w:tr[.//w:t[text()="Repeated header"]]', namespaces=wn)[0]
    assert exported_header_row.find('w:trPr/w:tblHeader', namespaces=wn) is not None
    exported_heights = exported_header_table.xpath('.//w:trPr/w:trHeight', namespaces=wn)
    assert [(h.get('{%s}val' % W), h.get('{%s}hRule' % W)) for h in exported_heights] == [
        ('1200', 'atLeast'), ('675', 'atLeast'), ('675', 'atLeast')]
    exported_named_tabs = exported_styles.xpath('//w:style[w:name/@w:val="Named tabs"]//w:tab', namespaces=wn)
    assert [(tab.get('{%s}val' % W), tab.get('{%s}pos' % W)) for tab in exported_named_tabs] == [
        ('center', '4800'), ('right', '9600')]
    exported_override = exported_document.xpath(
        '//w:p[.//w:t[text()="Named tab override"]]', namespaces=wn)[0]
    exported_override_tabs = exported_override.xpath('./w:pPr/w:tabs/w:tab', namespaces=wn)
    assert [(tab.get('{%s}val' % W), tab.get('{%s}pos' % W)) for tab in exported_override_tabs] == [
        ('right', '9200'), ('center', '4800')]
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
        plain_pdf_text = subprocess.run(['pdftotext', '-layout', str(pdf), '-'],
                                        capture_output=True, text=True, check=True).stdout
        for word in ('Martino', 'Motivazione', 'firmata', 'attribuito', 'attività'):
            assert word in plain_pdf_text, f'ligature-shaped word {word!r} was not extracted'
        entry = next(m for m in re.finditer(r'<line[^>]*xMax="([\d.]+)"[^>]*>(.*?)</line>', markup, re.S)
                     if 'Entry' in m.group(2))
        layout = roots[0].xpath('//style:page-layout/style:page-layout-properties', namespaces=NS)[0]
        right_edge = points(layout.get(qn_odf('fo:page-width'))) - points(layout.get(qn_odf('fo:margin-right')))
        assert float(entry.group(1)) <= right_edge + 0.5, \
            'TOC page number painted at %s, past the %s pt right margin' % (entry.group(1), right_edge)

    print('PASS: contextual spacing, border padding, table defaults/direct overrides, '
          'cell widths/grid, table indent, header rows, named tab stops, '
          'margin-relative anchors, page margins, TOC entry formatting, hard breaks')


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
