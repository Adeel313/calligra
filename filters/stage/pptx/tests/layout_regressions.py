#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Exercise editable diagram geometry through Stage's real load/save path.

Run with the local Stage installation and stage_pptx_roundtrip harness built.
The fixture is generated here; no third-party presentation is required.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import xml.etree.ElementTree as ET
import zipfile

NS = {
    "office": "urn:oasis:names:tc:opendocument:xmlns:office:1.0",
    "draw": "urn:oasis:names:tc:opendocument:xmlns:drawing:1.0",
    "style": "urn:oasis:names:tc:opendocument:xmlns:style:1.0",
    "svg": "urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0",
    "text": "urn:oasis:names:tc:opendocument:xmlns:text:1.0",
    "fo": "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0",
    "xlink": "http://www.w3.org/1999/xlink",
    "drawooo": "http://openoffice.org/2010/draw",
}
DECLARATIONS = " ".join(f'xmlns:{prefix}="{uri}"' for prefix, uri in NS.items())
ODP = "application/vnd.oasis.opendocument.presentation"


def fixture(path):
    styles = f'''<office:document-styles {DECLARATIONS} office:version="1.2">
      <office:styles>
        <draw:gradient draw:name="black" draw:style="linear" draw:start-color="#000000"
          draw:end-color="#000000" draw:start-intensity="100%" draw:end-intensity="100%" draw:angle="270deg"/>
        <draw:opacity draw:name="fade" draw:style="linear" draw:start="0%" draw:end="100%" draw:angle="270deg"/>
      </office:styles>
      <office:automatic-styles><style:page-layout style:name="page">
        <style:page-layout-properties fo:page-width="720pt" fo:page-height="540pt" style:print-orientation="landscape"/>
      </style:page-layout></office:automatic-styles>
      <office:master-styles><style:master-page style:name="Default" style:page-layout-name="page"/></office:master-styles>
    </office:document-styles>'''
    content = f'''<office:document-content {DECLARATIONS} office:version="1.2">
      <office:automatic-styles>
        <style:style style:name="blue" style:family="graphic"><style:graphic-properties draw:fill="solid" draw:fill-color="#165c87" draw:stroke="none"/></style:style>
        <style:style style:name="mask" style:family="graphic"><style:graphic-properties draw:fill="gradient" draw:fill-gradient-name="black" draw:opacity-name="fade" draw:stroke="none"/></style:style>
        <style:style style:name="fit" style:family="graphic"><style:graphic-properties draw:fill="none" draw:stroke="none" draw:auto-grow-height="false" draw:auto-grow-width="false" draw:fit-to-size="false" style:shrink-to-fit="true"/></style:style>
        <style:style style:name="large" style:family="paragraph"><style:text-properties fo:font-size="24pt"/></style:style>
      </office:automatic-styles>
      <office:body><office:presentation><draw:page draw:name="Layout" draw:master-page-name="Default">
        <draw:custom-shape draw:name="rounded" draw:style-name="blue" svg:x="20pt" svg:y="20pt" svg:width="140pt" svg:height="80pt">
          <draw:enhanced-geometry svg:viewBox="0 0 140 80" draw:type="ooxml-roundRect"
            draw:enhanced-path="M 0 20 L 120 0 140 60 20 80 Z N"
            drawooo:enhanced-path="M 0 20 G 20 20 180 90 L 120 0 G 20 20 270 90 L 140 60 G 20 20 0 90 L 20 80 G 20 20 90 90 Z N"/>
        </draw:custom-shape>
        <draw:custom-shape draw:name="raw-coordinates" draw:style-name="blue" svg:x="200pt" svg:y="20pt" svg:width="100pt" svg:height="50pt">
          <draw:enhanced-geometry svg:viewBox="0 0 0 0" draw:type="ooxml-non-primitive"
            drawooo:sub-view-size="400000 200000" draw:enhanced-path="M 0 0 L 400000 0 400000 200000 0 200000 Z N"/>
        </draw:custom-shape>
        <draw:custom-shape draw:name="gradient-mask" draw:style-name="mask" svg:x="400pt" svg:y="20pt" svg:width="150pt" svg:height="80pt">
          <draw:enhanced-geometry svg:viewBox="0 0 150 80" draw:type="rectangle" draw:enhanced-path="M 0 0 L 150 0 150 80 0 80 Z N"/>
        </draw:custom-shape>
        <draw:frame draw:name="svg-frame" svg:x="400pt" svg:y="200pt" svg:width="100pt" svg:height="100pt">
          <draw:image xlink:href="Pictures/icon.svg" xlink:type="simple"/>
        </draw:frame>
        <draw:frame draw:name="svg-single" svg:x="550pt" svg:y="200pt" svg:width="100pt" svg:height="100pt">
          <draw:image xlink:href="Pictures/single.svg" xlink:type="simple"/>
        </draw:frame>
        <draw:frame draw:name="svg-nested" svg:x="400pt" svg:y="350pt" svg:width="100pt" svg:height="100pt">
          <draw:image xlink:href="Pictures/nested.svg" xlink:type="simple"/>
        </draw:frame>
        <draw:frame draw:name="text-fit" draw:style-name="fit" svg:x="20pt" svg:y="200pt" svg:width="180pt" svg:height="50pt">
          <draw:text-box><text:p text:style-name="large">Text must fit its original box without covering adjacent diagrams.</text:p></draw:text-box>
        </draw:frame>
      </draw:page></office:presentation></office:body>
    </office:document-content>'''
    svg = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100"><path d="M10 10H90V90H10Z"/><path fill="white" d="M30 30H70V70H30Z"/></svg>'
    single = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100"><path d="M10 10H90V90H10Z"/></svg>'
    nested = svg.replace('><path', '><g><path', 1).replace('</svg>', '</g></svg>')
    manifest = f'''<manifest:manifest xmlns:manifest="urn:oasis:names:tc:opendocument:xmlns:manifest:1.0" manifest:version="1.2">
      <manifest:file-entry manifest:full-path="/" manifest:media-type="{ODP}"/>
      <manifest:file-entry manifest:full-path="content.xml" manifest:media-type="text/xml"/>
      <manifest:file-entry manifest:full-path="styles.xml" manifest:media-type="text/xml"/>
      <manifest:file-entry manifest:full-path="Pictures/icon.svg" manifest:media-type="image/svg+xml"/>
      <manifest:file-entry manifest:full-path="Pictures/single.svg" manifest:media-type="image/svg+xml"/>
      <manifest:file-entry manifest:full-path="Pictures/nested.svg" manifest:media-type="image/svg+xml"/>
    </manifest:manifest>'''
    with zipfile.ZipFile(path, "w") as archive:
        for name, data in {"mimetype": ODP, "content.xml": content, "styles.xml": styles,
                           "Pictures/icon.svg": svg, "Pictures/single.svg": single, "Pictures/nested.svg": nested,
                           "META-INF/manifest.xml": manifest}.items():
            archive.writestr(name, data)


def check(path):
    with zipfile.ZipFile(path) as archive:
        roots = [ET.fromstring(archive.read(name)) for name in ("content.xml", "styles.xml")]
    shapes = {element.get(f'{{{NS["draw"]}}}name'): element for element in roots[0].iter()
              if element.get(f'{{{NS["draw"]}}}name')}
    geometry = shapes["rounded"].find("draw:enhanced-geometry", NS)
    assert "G" in geometry.get(f'{{{NS["drawooo"]}}}enhanced-path', ""), "editable rounded corners lost"
    assert "W" in geometry.get(f'{{{NS["draw"]}}}enhanced-path', ""), "standard arc fallback lost"
    geometry = shapes["raw-coordinates"].find("draw:enhanced-geometry", NS)
    assert geometry.get(f'{{{NS["svg"]}}}viewBox') == "0 0 400000 200000", "custom path coordinate system lost"
    for name, count, left, top in (("svg-frame", 2, 400, 200), ("svg-single", 1, 550, 200), ("svg-nested", 2, 400, 350)):
        paths = shapes[name].findall(".//draw:path", NS)
        assert len(paths) == count, "SVG must remain editable vector shapes"
        for shape in paths:
            check_svg_bounds(shape, left, top)
    styles = {element.get(f'{{{NS["style"]}}}name'): element for root in roots for element in root.iter()
              if element.tag == f'{{{NS["style"]}}}style'}
    text_style = styles[shapes["text-fit"].get(f'{{{NS["draw"]}}}style-name')]
    # TextShape saves uniform autofit as shrink-to-fit, avoiding glyph stretching.
    properties = text_style.find("style:graphic-properties", NS)
    assert properties.get(f'{{{NS["draw"]}}}fit-to-size') == "false", "uniform fitting became glyph stretching"
    assert properties.get(f'{{{NS["style"]}}}shrink-to-fit') == "true", "text fitting lost"
    opacity = [float(stop.get(f'{{{NS["svg"]}}}stop-opacity', "1"))
               for root in roots for stop in root.findall(".//svg:stop", NS)]
    assert opacity and min(opacity) < 0.01 and max(opacity) > 0.99, "gradient transparency lost"


def check_svg_bounds(shape, left, top):
        numbers = lambda value: [float(v) for v in re.findall(r"[-+]?(?:\d*\.\d+|\d+)(?:[eE][-+]?\d+)?", value)]
        width = numbers(shape.get(f'{{{NS["svg"]}}}width'))[0]
        height = numbers(shape.get(f'{{{NS["svg"]}}}height'))[0]
        matrix = shape.get(f'{{{NS["draw"]}}}transform')
        if matrix:
            a, b, c, d, x, y = numbers(matrix)
        else:
            a, b, c, d = 1, 0, 0, 1
            x = numbers(shape.get(f'{{{NS["svg"]}}}x', "0"))[0]
            y = numbers(shape.get(f'{{{NS["svg"]}}}y', "0"))[0]
        for px, py in ((0, 0), (width, 0), (0, height), (width, height)):
            assert left - 1 <= a * px + c * py + x <= left + 101, "SVG escaped its frame horizontally"
            assert top - 1 <= b * px + d * py + y <= top + 101, "SVG escaped its frame vertically"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    repository = Path(__file__).resolve().parents[4]
    with tempfile.TemporaryDirectory(prefix="stage-layout-regressions-") as temporary:
        output = args.output or Path(temporary)
        output.mkdir(parents=True, exist_ok=True)
        source = output / "fixture.odp"
        fixture(source)
        env = dict(os.environ)
        env.setdefault("QT_QPA_PLATFORM", "offscreen")
        env.setdefault("QT_LOGGING_RULES", "*.debug=false")
        for iteration in range(2):
            destination = output / f"round-{iteration}"
            with (output / f"round-{iteration}.log").open("w") as log:
                subprocess.run([repository / "build/bin/stage_pptx_roundtrip", source, destination],
                               env=env, stdout=log, stderr=log, check=True, timeout=120)
            result = json.loads((destination / "stage-result.json").read_text())
            assert result["opened"] and result["odp_saved"] and result["pptx_saved"], result
            source = destination / "stage.odp"
            check(source)
        print("PASS editable arcs, custom coordinates, SVG placement, transparency and text fitting across reload")


if __name__ == "__main__":
    main()
