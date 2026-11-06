#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Regression tests for presentation feature extraction."""
from pathlib import Path
import tempfile
import unittest
import zipfile
from compatibility import features, canonical_math_text
from lxml import etree as E


class SlideCounting(unittest.TestCase):
    def test_group_character_and_delimiter(self):
        root = E.fromstring('''<a:p xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main"
            xmlns:a14="http://schemas.microsoft.com/office/drawing/2010/main"
            xmlns:m="http://schemas.openxmlformats.org/officeDocument/2006/math">
            <a14:m><m:oMath><m:groupChr><m:groupChrPr><m:chr m:val="⇔"/></m:groupChrPr>
            <m:e/></m:groupChr><m:d><m:e><m:r><m:t>x</m:t></m:r></m:e></m:d></m:oMath></a14:m></a:p>''')
        canonical_math_text(root)
        self.assertEqual(''.join(root.itertext()).strip(), '⇔(x)')

    def test_section_references_do_not_add_slides(self):
        presentation = b'''<p:presentation
            xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main"
            xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"
            xmlns:p14="http://schemas.microsoft.com/office/powerpoint/2010/main">
          <p:sldIdLst><p:sldId id="256" r:id="rId1"/></p:sldIdLst>
          <p:extLst><p:ext uri="section-list"><p14:sectionLst>
            <p14:section name="Section"><p14:sldIdLst>
              <p14:sldId id="256"/>
            </p14:sldIdLst></p14:section>
          </p14:sectionLst></p:ext></p:extLst>
        </p:presentation>'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'sections.pptx'
            with zipfile.ZipFile(path, 'w') as package:
                package.writestr('ppt/presentation.xml', presentation)
            self.assertEqual(features(path)['slides'], 1)


    def test_alternate_table_and_math_count_once(self):
        slide = b'''<p:sld xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main"
          xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main"
          xmlns:mc="http://schemas.openxmlformats.org/markup-compatibility/2006"
          xmlns:a14="http://schemas.microsoft.com/office/drawing/2010/main"
          xmlns:m="http://schemas.openxmlformats.org/officeDocument/2006/math">
          <p:cSld><p:spTree><mc:AlternateContent><mc:Choice Requires="a14">
            <p:graphicFrame><a:tbl><a:tr><a:tc><a:p><a14:m><m:oMath><m:d><m:e>
              <m:r><m:t>x</m:t></m:r></m:e></m:d></m:oMath></a14:m></a:p></a:tc></a:tr></a:tbl></p:graphicFrame>
          </mc:Choice><mc:Fallback><p:graphicFrame><a:tbl><a:tr><a:tc><a:p><a:r><a:t>fallback</a:t></a:r></a:p></a:tc></a:tr></a:tbl></p:graphicFrame></mc:Fallback></mc:AlternateContent></p:spTree></p:cSld></p:sld>'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'alternates.pptx'
            with zipfile.ZipFile(path, 'w') as package:
                package.writestr('ppt/presentation.xml', '<p:presentation xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main"><p:sldIdLst><p:sldId id="256"/></p:sldIdLst></p:presentation>')
                package.writestr('ppt/slides/slide1.xml', slide)
            result = features(path)
            self.assertEqual(result['tables'], 1)
            self.assertEqual(result['text'], '(x)')
            self.assertEqual(result['table_cells'], [[[0, 0, 1, 1, '(x)']]])


if __name__ == '__main__':
    unittest.main()
