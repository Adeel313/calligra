#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Verify editable MathML survives repeated binary PowerPoint saves."""
import argparse
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET
from zipfile import ZipFile

from export_roundtrip_regressions import FIXTURE, ROOT, DRAW, environment

MATH = 'http://www.w3.org/1998/Math/MathML'
FRAME = b'''
    <draw:frame draw:layer="layout" svg:x="2cm" svg:y="9cm" svg:width="5cm" svg:height="3cm">
     <draw:object>
      <math:math xmlns:math="http://www.w3.org/1998/Math/MathML">
       <math:mfrac>
        <math:msub><math:mi>x</math:mi><math:mn>7</math:mn></math:msub>
        <math:mn>2</math:mn>
       </math:mfrac>
      </math:math>
     </draw:object>
    </draw:frame>
'''


def rewrite_content(source, target, edit):
    with ZipFile(source) as original, ZipFile(target, 'w') as updated:
        for info in original.infolist():
            data = original.read(info)
            if info.filename == 'content.xml':
                data = edit(data)
            updated.writestr(info, data)


def formula(root):
    return root.find(f'.//{{{MATH}}}math')


def change_subscript(data):
    root = ET.fromstring(data)
    for frame in root.findall(f'.//{{{DRAW}}}frame'):
        obj = formula(frame)
        if obj is None:
            continue
        obj.find(f'.//{{{MATH}}}msub/{{{MATH}}}mn').text = '9'
        # Stage's formula editor drops the saved OLE payload when an edit
        # changes the formula. Mirror that state in the converter fixture.
        for name in list(frame.attrib):
            if name.endswith('}ppt-embedded-object'):
                del frame.attrib[name]
        return ET.tostring(root, encoding='utf-8')
    raise AssertionError('Editable equation not found')


def convert(source, target, out):
    with (out / (target.name + '.log')).open('w') as log:
        subprocess.run(
            [ROOT / 'build/bin/calligraconverter', '--batch', source, target],
            env=environment(), stdout=log, stderr=log, check=True, timeout=120,
        )


def check_formula(path, subscript):
    with ZipFile(path) as archive:
        root = ET.fromstring(archive.read('content.xml'))
    obj = formula(root)
    assert obj is not None, 'The formula reopened only as a picture'
    fraction = obj.find(f'.//{{{MATH}}}mfrac')
    assert fraction is not None, 'The fraction lost its structure'
    script = fraction.find(f'{{{MATH}}}msub')
    assert script is not None and script.find(f'{{{MATH}}}mn').text == subscript
    assert fraction.find(f'{{{MATH}}}mn').text == '2'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    out = parser.parse_args().output.resolve()
    out.mkdir(parents=True, exist_ok=False)

    source = out / 'formula.odp'
    rewrite_content(FIXTURE, source, lambda data: data.replace(b'</draw:page>', FRAME + b'</draw:page>', 1))
    for revision in ('7', '9'):
        ppt = out / f'formula-{revision}.ppt'
        imported = out / f'formula-{revision}-reopened.odp'
        convert(source, ppt, out)
        convert(ppt, imported, out)
        check_formula(imported, revision)
        if revision == '7':
            source = out / 'formula-edited.odp'
            rewrite_content(imported, source, change_subscript)
    print('PASS: native fractions and edited subscripts survive repeated binary PPT saves')


if __name__ == '__main__':
    main()
