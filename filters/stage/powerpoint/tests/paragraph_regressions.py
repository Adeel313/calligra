#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Regression for binary .ppt paragraph-formatting export fidelity.

Builds a small ODP containing a character bullet list and a paragraph with
explicit line spacing and space before/after, exports it to a binary .ppt with
Calligra's native filter, re-imports it and checks that all of those properties
survive as ODF automatic styles.

This guards the TextPFException writer in PptExport.cpp (bullet flags, bullet
character/font/colour/size, left margin/indent and spacing fields) and the
importer's list and paragraph property mapping in PptToOdp.cpp.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
BASE = HERE / 'data' / 'export_roundtrip.odp'

FONT_FACES = (
    '<office:font-face-decls>'
    '<style:font-face style:name="Arial" svg:font-family="Arial"/>'
    '</office:font-face-decls>'
)

AUTO_STYLES = (
    '<text:list-style style:name="BulletList">'
    '<text:list-level-style-bullet text:level="1" text:bullet-char="&#8226;">'
    '<style:list-level-properties text:space-before="0.5cm" text:min-label-width="0.5cm"/>'
    '<style:text-properties style:font-name="Arial" fo:font-size="12pt" fo:color="#008000"/>'
    '</text:list-level-style-bullet></text:list-style>'
    '<style:style style:name="Pspace" style:family="paragraph">'
    '<style:paragraph-properties fo:line-height="150%" fo:margin-top="12pt" '
    'fo:margin-bottom="6pt"/>'
    '</style:style>'
    '<style:style style:name="Pindent" style:family="paragraph">'
    '<style:paragraph-properties fo:margin-left="1cm" fo:text-indent="0.5cm"/>'
    '</style:style>'
)


def environment():
    install = ROOT / 'build/install'
    return dict(os.environ,
                QT_QPA_PLATFORM='offscreen', QT_QPA_PLATFORMTHEME='generic',
                QT_LOGGING_RULES='*.debug=false', GSETTINGS_BACKEND='memory',
                LD_LIBRARY_PATH=str(ROOT / 'build/bin') + ':' + str(install / 'lib/x86_64-linux-gnu'),
                QT_PLUGIN_PATH=str(install / 'lib/x86_64-linux-gnu/qt6/plugins'),
                XDG_DATA_DIRS=str(install / 'share') + ':/usr/local/share:/usr/share')


def build_paragraph_odp(source, target):
    with zipfile.ZipFile(source) as zin, zipfile.ZipFile(target, 'w') as zout:
        for entry in zin.infolist():
            data = zin.read(entry.filename)
            if entry.filename == 'content.xml':
                text = data.decode()
                if 'xmlns:fo=' not in text:
                    text = text.replace(
                        'xmlns:xlink=',
                        'xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" '
                        'xmlns:xlink=', 1)
                text = text.replace('<office:automatic-styles/>',
                                    FONT_FACES + '<office:automatic-styles>' + AUTO_STYLES
                                    + '</office:automatic-styles>', 1)
                text = text.replace(
                    '<text:p>ExportRoundTripTitle</text:p>',
                    '<text:list text:style-name="BulletList">'
                    '<text:list-item><text:p>ExportRoundTripTitle</text:p></text:list-item>'
                    '<text:list-item><text:p>FirstBodyLine</text:p></text:list-item>'
                    '</text:list>', 1)
                text = text.replace('<text:p>SecondBodyLine</text:p>',
                                    '<text:p text:style-name="Pspace">SecondBodyLine</text:p>', 1)
                text = text.replace('<text:p>SecondSlideText</text:p>',
                                    '<text:p text:style-name="Pindent">SecondSlideText</text:p>', 1)
                data = text.encode()
            zout.writestr(entry, data)


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    out = parser.parse_args().output.resolve()
    out.mkdir(parents=True, exist_ok=False)

    source = out / 'paragraphs.odp'
    ppt = out / 'paragraphs.ppt'
    reimported = out / 'paragraphs_rt.odp'
    build_paragraph_odp(BASE, source)

    with (out / 'export.log').open('w') as log:
        subprocess.run([ROOT / 'build/bin/calligraconverter', '--batch',
                        '--mimetype', 'application/vnd.ms-powerpoint', source, ppt],
                       env=environment(), stdout=log, stderr=log, check=True, timeout=120)
    with (out / 'import.log').open('w') as log:
        subprocess.run([ROOT / 'build/bin/ppttoodp', ppt, reimported],
                       env=environment(), stdout=log, stderr=log, check=True, timeout=120)

    import_log = (out / 'import.log').read_text()
    for fatal in ('error parsing PowerPointStructs', 'bytes left at the end of PowerPointStructs'):
        check(fatal not in import_log, 'round-trip import failed: ' + fatal)

    with zipfile.ZipFile(reimported) as package:
        content = package.read('content.xml').decode()

    # Both list items must still be a real bullet list, not plain paragraphs.
    check(content.count('<text:list ') >= 1, 'bullet list was lost in round-trip')
    check(content.count('<text:list-item') >= 2, 'list items were lost in round-trip')

    bullet = ''
    for match in re.finditer(r'<text:list-style\b.*?</text:list-style>', content, re.S):
        if 'text:list-level-style-bullet' in match.group(0):
            bullet = match.group(0)
            break
    check(bullet, 'no bullet list style in re-imported ODP')
    for label, prop in [
        ('bullet character', 'text:bullet-char="\u2022"'),
        ('bullet colour', 'fo:color="#008000"'),
        ('bullet font', 'fo:font-family="Arial"'),
        ('bullet size', 'fo:font-size="12pt"'),
    ]:
        check(prop in bullet, '%s lost in round-trip' % label)
    for label, prop in [
        ('list indent', 'text:space-before="0.498'),
        ('label width', 'text:min-label-width="0.502'),
    ]:
        check(prop in bullet, '%s lost in round-trip' % label)

    # Line spacing and space before/after must be preserved independently.
    spaced = ''
    for match in re.finditer(r'<style:style\b[^>]*style:family="paragraph"[^>]*>.*?</style:style>',
                             content, re.S):
        if 'fo:line-height="150%"' in match.group(0):
            spaced = match.group(0)
            break
    check(spaced, 'line spacing lost in round-trip')
    check('fo:margin-top="0.423' in spaced, 'space-before lost in round-trip')
    check('fo:margin-bottom="0.211' in spaced, 'space-after lost in round-trip')

    # Plain paragraphs must keep their left margin and first-line indent
    # without being turned into lists.
    indented = ''
    for match in re.finditer(r'<style:style\b[^>]*style:family="paragraph"[^>]*>.*?</style:style>',
                             content, re.S):
        if 'fo:margin-left="1.001' in match.group(0):
            indented = match.group(0)
            break
    check(indented, 'left margin lost in round-trip')
    check('fo:text-indent="0.498' in indented, 'first-line indent lost in round-trip')

    check('ExportRoundTripTitle' in content and 'FirstBodyLine' in content
          and 'SecondBodyLine' in content and 'SecondSlideText' in content,
          'paragraph text lost')

    print('PASS: binary .ppt round-trip preserves bullets, indentation and paragraph spacing')


if __name__ == '__main__':
    main()
