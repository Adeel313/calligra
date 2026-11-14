#!/usr/bin/env python3
"""Generates the colour test fixtures in this folder.

    python3 tests/make_color_fixtures.py

Writes docx_11_colors.docx, pptx_11_colors.pptx and xlsx_12_colors.xlsx. Each
file uses explicit RGB colours, theme colours with tint or shade, and cell or
shape fills. The XLSX also carries conditional formats. check_colors.py reads
the expected values from the same definitions.
"""
import os

from docx import Document
from docx.enum.text import WD_COLOR_INDEX
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import RGBColor
from openpyxl import Workbook
from openpyxl.formatting.rule import CellIsRule, FormulaRule, Rule
from openpyxl.styles import Font, PatternFill
from openpyxl.styles.colors import Color
from openpyxl.styles.differential import DifferentialStyle
from pptx import Presentation
from pptx.dml.color import RGBColor as PptxRGB
from pptx.enum.dml import MSO_THEME_COLOR
from pptx.enum.shapes import MSO_SHAPE
from pptx.util import Inches, Pt

HERE = os.path.dirname(os.path.abspath(__file__))

# Theme colours of the Office 2007 palette the generators write.
THEME_HEX = {'accent1': '4F81BD', 'accent2': 'C0504D', 'accent3': '9BBB59', 'accent4': '8064A2'}


def docx_fixture(path):
    doc = Document()
    doc.add_heading('Colour test: DOCX', level=1)

    def run(p, text, color=None, highlight=None, shade=None, theme=None, tint=None, theme_shade=None):
        r = p.add_run(text)
        rpr = r._r.get_or_add_rPr()
        if color or theme:
            c = OxmlElement('w:color')
            c.set(qn('w:val'), color or THEME_HEX[theme])
            if theme:
                c.set(qn('w:themeColor'), theme)
            if tint:
                c.set(qn('w:themeTint'), tint)
            if theme_shade:
                c.set(qn('w:themeShade'), theme_shade)
            rpr.append(c)
        if highlight:
            h = OxmlElement('w:highlight')
            h.set(qn('w:val'), highlight)
            rpr.append(h)
        if shade:
            s = OxmlElement('w:shd')
            s.set(qn('w:val'), 'clear')
            s.set(qn('w:color'), 'auto')
            s.set(qn('w:fill'), shade)
            rpr.append(s)
        return r

    p = doc.add_paragraph('Run colours: ')
    run(p, 'red ', color='FF0000')
    run(p, 'green ', color='00B050')
    run(p, 'blue', color='0070C0')

    p = doc.add_paragraph('Highlights: ')
    run(p, 'yellow ', highlight='yellow')
    run(p, 'turquoise ', highlight='cyan')
    run(p, 'pink', highlight='magenta')

    p = doc.add_paragraph('Run shading: ')
    run(p, 'pale red ', shade='FFC7CE')
    run(p, 'pale green', shade='C6EFCE')

    p = doc.add_paragraph('Theme colours: ')
    run(p, 'accent1 ', theme='accent1')
    run(p, 'accent2 tint ', theme='accent2', tint='66')
    run(p, 'accent3 darker 25%', theme='accent3', theme_shade='BF')

    p = doc.add_paragraph()
    p.add_run('Paragraph shading, dark text on light fill.').font.color.rgb = RGBColor(0x1F, 0x38, 0x64)
    ppr = p._p.get_or_add_pPr()
    shd = OxmlElement('w:shd')
    shd.set(qn('w:val'), 'clear')
    shd.set(qn('w:color'), 'auto')
    shd.set(qn('w:fill'), 'DDEBF7')
    ppr.append(shd)

    table = doc.add_table(rows=4, cols=3)
    table.style = 'Table Grid'
    fills = [
        [('Solid RGB', 'FFC000'), ('Theme accent1', 'theme:accent1'), ('Gray', 'D9D9D9')],
        [('Light green', 'E2EFDA'), ('Dark blue', '1F4E79'), ('Light grey', 'F2F2F2')],
        [('Pale red', 'FFC7CE'), ('Pale green', 'C6EFCE'), ('Light blue', 'DDEBF7')],
        [('Accent2 lighter 50%', 'theme:accent2:80'), ('Accent3 darker 25%', 'theme:accent3:-BF'), ('White', 'FFFFFF')],
    ]
    for r_idx, row in enumerate(fills):
        for c_idx, (text, fill) in enumerate(row):
            cell = table.cell(r_idx, c_idx)
            cell.text = text
            tcpr = cell._tc.get_or_add_tcPr()
            shd = OxmlElement('w:shd')
            shd.set(qn('w:val'), 'clear')
            shd.set(qn('w:color'), 'auto')
            if fill.startswith('theme:'):
                name, _, modifier = fill[6:].partition(':')
                shd.set(qn('w:fill'), THEME_HEX[name])
                shd.set(qn('w:themeFill'), name)
                if modifier.startswith('-'):
                    shd.set(qn('w:themeFillShade'), modifier[1:])
                elif modifier:
                    shd.set(qn('w:themeFillTint'), modifier)
            else:
                shd.set(qn('w:fill'), fill)
            tcpr.append(shd)
    doc.save(path)


def pptx_fixture(path):
    prs = Presentation()
    prs.slide_width = Inches(13.333)
    prs.slide_height = Inches(7.5)
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    slide.background.fill.solid()
    slide.background.fill.fore_color.rgb = PptxRGB.from_string('E2EFDA')

    box = slide.shapes.add_textbox(Inches(0.5), Inches(0.4), Inches(12), Inches(0.8))
    p = box.text_frame.paragraphs[0]
    r = p.add_run()
    r.text = 'Colour test: PPTX'
    r.font.size = Pt(28)
    r.font.color.rgb = PptxRGB(0x1F, 0x38, 0x64)

    box = slide.shapes.add_textbox(Inches(0.5), Inches(1.4), Inches(12), Inches(0.6))
    p = box.text_frame.paragraphs[0]
    for text, rgb in (('red ', 'FF0000'), ('green ', '00B050'), ('blue', '0070C0')):
        r = p.add_run()
        r.text = text
        r.font.size = Pt(24)
        r.font.color.rgb = PptxRGB.from_string(rgb)

    box = slide.shapes.add_textbox(Inches(0.5), Inches(2.1), Inches(12), Inches(0.6))
    p = box.text_frame.paragraphs[0]
    r = p.add_run()
    r.text = 'accent1, lighter 40%  '
    r.font.size = Pt(24)
    r.font.color.theme_color = MSO_THEME_COLOR.ACCENT_1
    r.font.color.brightness = 0.4
    r = p.add_run()
    r.text = 'accent2, darker 25%'
    r.font.size = Pt(24)
    r.font.color.theme_color = MSO_THEME_COLOR.ACCENT_2
    r.font.color.brightness = -0.25

    shapes = [
        (0.5, 3.0, 'FFC000', None, None),
        (3.5, 3.0, None, MSO_THEME_COLOR.ACCENT_2, -0.25),
        (6.5, 3.0, '1F4E79', None, None),
        (9.5, 3.0, None, MSO_THEME_COLOR.ACCENT_1, 0.6),
    ]
    for left, top, rgb, theme, brightness in shapes:
        shp = slide.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(left), Inches(top), Inches(2.6), Inches(1.6))
        shp.fill.solid()
        if rgb:
            shp.fill.fore_color.rgb = PptxRGB.from_string(rgb)
        else:
            shp.fill.fore_color.theme_color = theme
            shp.fill.fore_color.brightness = brightness
        shp.line.fill.background()

    table = slide.shapes.add_table(3, 3, Inches(0.5), Inches(5.0), Inches(9), Inches(2.1)).table
    cell_fills = [['FFC000', 'C6EFCE', 'FFC7CE'], ['DDEBF7', 'D9D9D9', '1F4E79'], ['E2EFDA', 'F2F2F2', 'FFFFFF']]
    for r_idx, row in enumerate(cell_fills):
        for c_idx, rgb in enumerate(row):
            cell = table.cell(r_idx, c_idx)
            cell.text = rgb
            cell.fill.solid()
            cell.fill.fore_color.rgb = PptxRGB.from_string(rgb)
    prs.save(path)


def xlsx_fixture(path):
    wb = Workbook()
    ws = wb.active
    ws.title = 'Colours'
    ws['A1'] = 'Explicit fills'
    ws['A2'] = 'Red'
    ws['A2'].fill = PatternFill('solid', fgColor='FF0000')
    ws['B2'] = 'Green'
    ws['B2'].fill = PatternFill('solid', fgColor='00B050')
    ws['C2'] = 'Blue'
    ws['C2'].fill = PatternFill('solid', fgColor='0070C0')
    ws['D2'] = 'Yellow font'
    ws['D2'].fill = PatternFill('solid', fgColor='1F4E79')
    ws['D2'].font = Font(color='FFFF00')

    ws['A4'] = 'Theme fills'
    ws['A5'] = 'accent1'
    ws['A5'].fill = PatternFill('solid', fgColor=Color(theme=4, tint=0.0))
    ws['B5'] = 'accent2, lighter 40%'
    ws['B5'].fill = PatternFill('solid', fgColor=Color(theme=5, tint=0.4))
    ws['C5'] = 'accent3, darker 25%'
    ws['C5'].fill = PatternFill('solid', fgColor=Color(theme=6, tint=-0.25))
    ws['D5'] = 'lt2, darker 50%'
    ws['D5'].fill = PatternFill('solid', fgColor=Color(theme=2, tint=-0.5))

    ws['A7'] = 'Conditional formats'
    for i, value in enumerate([10, 60, 95, 40, 75], start=8):
        ws.cell(row=i, column=1, value=value)
    for i, text in enumerate(['Pass', 'Fail', 'Pass with merit', 'Absent', 'Pass'], start=8):
        ws.cell(row=i, column=2, value=text)

    red = DifferentialStyle(font=Font(color='9C0006'), fill=PatternFill(bgColor='FFC7CE'))
    ws.conditional_formatting.add('A8:A12', Rule(type='cellIs', operator='greaterThan', formula=['50'], dxf=red))

    green = DifferentialStyle(fill=PatternFill(bgColor='C6EFCE'))
    ws.conditional_formatting.add('B8:B12', Rule(type='containsText', operator='containsText', text='Pass',
                                                  formula=['NOT(ISERROR(SEARCH("Pass",B8)))'], dxf=green))

    yellow = DifferentialStyle(fill=PatternFill(bgColor='FFEB9C'))
    ws.conditional_formatting.add('A8:B12', Rule(type='expression', formula=['$A8<20'], dxf=yellow))

    ws.column_dimensions['A'].width = 24
    ws.column_dimensions['B'].width = 24
    ws.column_dimensions['C'].width = 24
    ws.column_dimensions['D'].width = 24
    wb.save(path)


def main():
    docx_fixture(os.path.join(HERE, 'docx_11_colors.docx'))
    pptx_fixture(os.path.join(HERE, 'pptx_11_colors.pptx'))
    xlsx_fixture(os.path.join(HERE, 'xlsx_12_colors.xlsx'))
    print('wrote docx_11_colors.docx, pptx_11_colors.pptx, xlsx_12_colors.xlsx')


if __name__ == '__main__':
    main()
