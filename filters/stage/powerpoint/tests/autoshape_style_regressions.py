#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Regression for native shape and text-box fidelity in binary PPT export.

A draw:custom-shape (imported as a KoTosContainer) carries both editable text
and a visual fill/outline.  The exporter used to flatten the shape, straight
lines and callouts into pictures, which kept the pixels but lost the editable
objects.  Ordinary draw:frame text boxes were written as msosptNil, which made
the PPT importer drop their ODF padding.

This test checks that custom shapes, lines and callouts come back as native
draw:custom-shape / draw:line objects, and that text boxes keep their type and
fo:padding values across an ODP -> PPT -> ODP round-trip.
"""
import argparse
import os
from pathlib import Path
import struct
import subprocess
import zipfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
BASE = HERE / 'data' / 'export_roundtrip.odp'

DRAW = 'urn:oasis:names:tc:opendocument:xmlns:drawing:1.0'
STYLE = 'urn:oasis:names:tc:opendocument:xmlns:style:1.0'
SVG = 'urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0'
FO = 'urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0'

MSOSPT_TEXT_BOX = 0x00ca
MSOSPT_BORDER_CALLOUT_1 = 0x002f
FOPT_ADJUST_1 = 0x0147
FOPT_LEFT_INSET = 0x0081
FOPT_TOP_INSET = 0x0082
FOPT_RIGHT_INSET = 0x0083
FOPT_BOTTOM_INSET = 0x0084


def environment():
    install = ROOT / 'build/install'
    return dict(os.environ,
                QT_QPA_PLATFORM='offscreen', QT_QPA_PLATFORMTHEME='generic',
                QT_LOGGING_RULES='*.debug=false', GSETTINGS_BACKEND='memory',
                LD_LIBRARY_PATH=str(ROOT / 'build/bin') + ':' + str(install / 'lib/x86_64-linux-gnu'),
                QT_PLUGIN_PATH=str(install / 'lib/x86_64-linux-gnu/qt6/plugins'),
                XDG_DATA_DIRS=str(install / 'share') + ':/usr/local/share:/usr/share')


CALLOUT = (
    '<draw:custom-shape draw:style-name="calloutLine" draw:layer="layout" '
    'svg:x="14cm" svg:y="10cm" svg:width="4cm" svg:height="2cm">'
    '<text:p>CalloutLabel</text:p>'
    '<draw:enhanced-geometry draw:modifiers="15000 12000 21000 9000" '
    'svg:viewBox="0 0 21600 21600" '
    'draw:enhanced-path="M 0 0 L 21600 0 21600 21600 0 21600 Z N M ?f0 ?f1 L ?f2 ?f3 N" '
    'draw:type="line-callout-1">'
    '<draw:equation draw:name="f0" draw:formula="$0 "/>'
    '<draw:equation draw:name="f1" draw:formula="$1 "/>'
    '<draw:equation draw:name="f2" draw:formula="$2 "/>'
    '<draw:equation draw:name="f3" draw:formula="$3 "/>'
    '<draw:equation draw:name="f4" draw:formula="$4 "/>'
    '<draw:equation draw:name="f5" draw:formula="$5 "/>'
    '<draw:equation draw:name="f6" draw:formula="$6 "/>'
    '<draw:equation draw:name="f7" draw:formula="$7 "/>'
    '<draw:handle draw:handle-position="$0 $1"/>'
    '<draw:handle draw:handle-position="$2 $3"/>'
    '</draw:enhanced-geometry>'
    '</draw:custom-shape>'
)


def build_autoshape_odp(source, target):
    shape = (
        '<draw:custom-shape draw:style-name="blueBox" draw:layer="layout" '
        'svg:x="2cm" svg:y="2cm" svg:width="10cm" svg:height="5cm">'
        '<text:p>BlueBoxText</text:p>'
        '<draw:enhanced-geometry svg:viewBox="0 0 21600 21600" '
        'draw:enhanced-path="M 0 0 L 21600 0 21600 21600 0 21600 0 0 Z N" '
        'draw:type="rectangle"/>'
        '</draw:custom-shape>'
    )
    line = (
        '<draw:line draw:style-name="redLine" draw:layer="layout" '
        'svg:x1="2cm" svg:y1="8cm" svg:x2="12cm" svg:y2="8cm"/>'
    )
    padded = (
        '<draw:frame draw:style-name="paddedText" draw:layer="layout" '
        'svg:x="2cm" svg:y="12cm" svg:width="10cm" svg:height="2cm">'
        '<draw:text-box><text:p>PaddedText</text:p></draw:text-box>'
        '</draw:frame>'
    )
    style = (
        '<style:style style:name="blueBox" style:family="graphic">'
        '<style:graphic-properties draw:fill="solid" draw:fill-color="#2040c0" '
        'draw:stroke="none"/>'
        '</style:style>'
        '<style:style style:name="redLine" style:family="graphic">'
        '<style:graphic-properties draw:fill="none" draw:stroke="solid" '
        'svg:stroke-color="#ff0000" svg:stroke-width="0.05cm"/>'
        '</style:style>'
        '<style:style style:name="calloutLine" style:family="graphic">'
        '<style:graphic-properties draw:fill="none" draw:stroke="none"/>'
        '</style:style>'
        '<style:style style:name="paddedText" style:family="graphic">'
        '<style:graphic-properties draw:fill="none" draw:stroke="none" '
        'fo:padding-left="7.2pt" fo:padding-right="3.6pt" '
        'fo:padding-top="1.8pt" fo:padding-bottom="0.9pt"/>'
        '</style:style>'
    )
    with zipfile.ZipFile(source) as zin, zipfile.ZipFile(target, 'w') as zout:
        for entry in zin.infolist():
            data = zin.read(entry.filename)
            if entry.filename == 'content.xml':
                text = data.decode()
                if 'xmlns:fo=' not in text:
                    text = text.replace('xmlns:xlink=',
                                        'xmlns:fo="' + FO + '" xmlns:xlink=', 1)
                text = text.replace('<office:automatic-styles/>',
                                    '<office:automatic-styles>' + style + '</office:automatic-styles>', 1)
                text = text.replace('<draw:page draw:master-page-name="M1" draw:name="Slide One">',
                                    '<draw:page draw:master-page-name="M1" draw:name="Slide One">'
                                    + shape + line + CALLOUT + padded, 1)
                data = text.encode()
            zout.writestr(entry, data)


class Record:
    def __init__(self, version, instance, kind, payload=b'', children=None):
        self.version = version
        self.instance = instance
        self.kind = kind
        self.payload = payload
        self.children = children or []

    @property
    def container(self):
        return self.version == 0x0f


def parse_records(data, offset=0, end=None):
    if end is None:
        end = len(data)
    result = []
    while offset + 8 <= end:
        version_instance, kind, size = struct.unpack_from('<HHI', data, offset)
        body = offset + 8
        if (version_instance & 0x0f) == 0x0f:
            children = parse_records(data, body, body + size)
            result.append(Record(version_instance & 0x0f, version_instance >> 4, kind, children=children))
        else:
            result.append(Record(version_instance & 0x0f, version_instance >> 4, kind,
                                 payload=data[body:body + size]))
        offset = body + size
    return result


def find_record(records, kind, instance=None):
    for record in records:
        if record.kind == kind and (instance is None or record.instance == instance):
            return record
        if record.container:
            found = find_record(record.children, kind, instance)
            if found is not None:
                return found
    return None


def fopt_entries(record):
    if record is None:
        return {}
    entries = {}
    for offset in range(0, len(record.payload) - 5, 6):
        opid, value = struct.unpack_from('<HI', record.payload, offset)
        entries[opid & 0x3fff] = value
    return entries


def shape_text(shape):
    client = find_record(shape.children, 0xf00d)
    if client is None:
        return ''
    text = find_record(client.children, 0x0fa0)
    return text.payload.decode('utf-16-le', 'ignore') if text is not None else ''


def ppt_shapes(path):
    import olefile  # the corpus harness already depends on this reader

    with olefile.OleFileIO(str(path)) as ole:
        records = parse_records(ole.openstream('PowerPoint Document').read())
    slide = find_record(records, 0x03ee)
    drawing = find_record(slide.children, 0x040c)
    container = find_record(drawing.children, 0xf002)
    group = find_record(container.children, 0xf003)
    return list(group.children)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    out = parser.parse_args().output.resolve()
    out.mkdir(parents=True, exist_ok=False)

    source = out / 'autoshape.odp'
    ppt = out / 'autoshape.ppt'
    reimported = out / 'autoshape_rt.odp'
    build_autoshape_odp(BASE, source)

    with (out / 'export.log').open('w') as log:
        subprocess.run([ROOT / 'build/bin/calligraconverter', '--batch',
                        '--mimetype', 'application/vnd.ms-powerpoint', source, ppt],
                       env=environment(), stdout=log, stderr=log, check=True, timeout=120)
    with (out / 'import.log').open('w') as log:
        subprocess.run([ROOT / 'build/bin/ppttoodp', ppt, reimported],
                       env=environment(), stdout=log, stderr=log, check=True, timeout=120)

    # Check the binary records before relying on the Calligra importer: the
    # importer intentionally zeroes padding on non-text-box shapes, so a wrong
    # F00A instance would silently hide a broken export.
    shapes_by_text = {}
    for shape in ppt_shapes(ppt):
        text = shape_text(shape).split('\r')[0]
        if text:
            shapes_by_text[text] = shape
    assert 'PaddedText' in shapes_by_text, 'text-box payload was not written to the PPT'
    padded_shape = shapes_by_text['PaddedText']
    placeholder = find_record(padded_shape.children, 0xf00a)
    assert placeholder is not None and placeholder.instance == MSOSPT_TEXT_BOX, \
        f'text box was written as instance {placeholder.instance if placeholder else None}, expected 0xca'
    insets = fopt_entries(find_record(padded_shape.children, 0xf00b))
    assert insets.get(FOPT_LEFT_INSET) == 91440, 'fo:padding-left was not written to FOPT'
    assert insets.get(FOPT_RIGHT_INSET) == 45720, 'fo:padding-right was not written to FOPT'
    assert insets.get(FOPT_TOP_INSET) == 22860, 'fo:padding-top was not written to FOPT'
    assert insets.get(FOPT_BOTTOM_INSET) == 11430, 'fo:padding-bottom was not written to FOPT'

    assert 'CalloutLabel' in shapes_by_text, 'callout text payload was not written to the PPT'
    callout_shape = shapes_by_text['CalloutLabel']
    callout_placeholder = find_record(callout_shape.children, 0xf00a)
    assert callout_placeholder is not None and callout_placeholder.instance == MSOSPT_BORDER_CALLOUT_1, \
        'callout was not written as a native border callout'
    adjustments = fopt_entries(find_record(callout_shape.children, 0xf00b))
    assert adjustments.get(FOPT_ADJUST_1) == 15000, 'callout adjustments were not read from its path child'

    with zipfile.ZipFile(reimported) as package:
        content = package.read('content.xml').decode()
        images = [n for n in package.namelist() if n.startswith('Pictures/')]

    root = ET.fromstring(content)
    shapes = list(root.iter(f'{{{DRAW}}}custom-shape'))
    assert len(shapes) == 2, f'expected autoshape and callout, found {len(shapes)}'
    by_geometry = {}
    for shape in shapes:
        geometry = shape.find(f'{{{DRAW}}}enhanced-geometry')
        if geometry is not None:
            by_geometry[geometry.get(f'{{{DRAW}}}type')] = shape
    rectangle = by_geometry.get('rectangle')
    assert rectangle is not None and 'BlueBoxText' in ''.join(rectangle.itertext()), \
        'rectangle autoshape or its text was lost'
    style_name = rectangle.get(f'{{{DRAW}}}style-name')
    properties = [e.find(f'{{{STYLE}}}graphic-properties') for e in root.iter(f'{{{STYLE}}}style')
                  if e.get(f'{{{STYLE}}}name') == style_name]
    assert any(p is not None and p.get(f'{{{DRAW}}}fill-color') == '#2040c0' for p in properties), \
        'autoshape fill was not preserved'
    callout = by_geometry.get('line-callout-1')
    assert callout is not None and 'CalloutLabel' in ''.join(callout.itertext()), \
        'callout label was lost on reload'

    padded_frames = [frame for frame in root.iter(f'{{{DRAW}}}frame')
                     if 'PaddedText' in ''.join(frame.itertext())]
    assert len(padded_frames) == 1, 'padded text frame was lost'
    padded_properties = [e.find(f'{{{STYLE}}}graphic-properties') for e in root.iter(f'{{{STYLE}}}style')
                         if e.get(f'{{{STYLE}}}name') == padded_frames[0].get(f'{{{DRAW}}}style-name')]
    def pt(value):
        return float(value[:-2]) if value and value.endswith('pt') else -1.0

    assert any(p is not None and pt(p.get(f'{{{FO}}}padding-left')) == 7.2 for p in padded_properties), \
        'text-box padding was zeroed by the PPT importer'

    lines = list(root.iter(f'{{{DRAW}}}line'))
    assert len(lines) == 1, f'straight line was not preserved (found {len(lines)})'
    assert len(images) <= 1, 'autoshape, callout or line was flattened into an extra picture'
    print('PASS: native shapes, callout label and text-box insets survive binary PPT round-trip')


if __name__ == '__main__':
    main()
