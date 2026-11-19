#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Generate three feature-diverse ODP sources and export each to binary .ppt.

The resulting .ppt files live next to this script in ``data/`` and are used by
``roundtrip_benchmark.py``.  Three feature profiles are produced:

  * features_text.ppt   - text-heavy: many text boxes, multi-paragraph,
                          unicode and long strings at varied positions.
  * features_images.ppt - image-heavy: PNG and JPEG images, several per slide,
                          with small text labels.
  * features_mixed.ppt  - mixed: text + images + many slides in one deck.

Usage:  python3 gen_feature_ppts.py [--output DIR]
"""
import argparse
import io
import os
from pathlib import Path
import subprocess
import zipfile

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
DATA = HERE / 'data'

MIMETYPE = 'application/vnd.oasis.opendocument.presentation'
MANIFEST = '''<?xml version="1.0" encoding="UTF-8"?>
<manifest:manifest xmlns:manifest="urn:oasis:names:tc:opendocument:xmlns:manifest:1.0" manifest:version="1.2">
 <manifest:file-entry manifest:full-path="/" manifest:version="1.2" manifest:media-type="{mime}"/>
 <manifest:file-entry manifest:full-path="content.xml" manifest:media-type="text/xml"/>
 <manifest:file-entry manifest:full-path="styles.xml" manifest:media-type="text/xml"/>
 <manifest:file-entry manifest:full-path="meta.xml" manifest:media-type="text/xml"/>
{images}</manifest:manifest>
'''
STYLES = '''<?xml version="1.0" encoding="UTF-8"?>
<office:document-styles xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0">
 <office:automatic-styles><style:page-layout style:name="PM1"><style:page-layout-properties fo:page-width="28cm" fo:page-height="15.75cm"/></style:page-layout></office:automatic-styles>
 <office:master-styles><style:master-page style:name="M1" style:page-layout-name="PM1"/></office:master-styles>
</office:document-styles>
'''
META = '<?xml version="1.0" encoding="UTF-8"?>\n<office:document-meta xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"/>\n'


def esc(s):
    return s.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')


def text_frame(x, y, w, h, paragraphs):
    ps = ''.join('<text:p>%s</text:p>' % esc(p) for p in paragraphs)
    return ('<draw:frame draw:layer="layout" svg:x="%scm" svg:y="%scm" '
            'svg:width="%scm" svg:height="%scm"><draw:text-box>%s</draw:text-box></draw:frame>'
            % (x, y, w, h, ps))


def image_frame(x, y, w, h, name):
    return ('<draw:frame draw:layer="layout" svg:x="%scm" svg:y="%scm" '
            'svg:width="%scm" svg:height="%scm"><draw:image xlink:href="Pictures/%s" '
            'xlink:type="simple"/></draw:frame>' % (x, y, w, h, name))


def build_odp(slides, images):
    """slides: list of lists of frame XML strings. images: name->bytes."""
    pages = ''.join('<draw:page draw:master-page-name="M1" draw:name="Slide %d">%s</draw:page>'
                    % (i + 1, ''.join(frames)) for i, frames in enumerate(slides))
    content = ('<?xml version="1.0" encoding="UTF-8"?>\n'
        '<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" '
        'xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" '
        'xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0" '
        'xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0" '
        'xmlns:xlink="http://www.w3.org/1999/xlink">'
        '<office:automatic-styles/><office:body><office:presentation>%s</office:presentation></office:body>'
        '</office:document-content>' % pages)
    manifest_images = ''.join(
        ' <manifest:file-entry manifest:full-path="Pictures/%s" manifest:media-type="%s"/>\n'
        % (name, 'image/png' if name.endswith('.png') else 'image/jpeg') for name in images)
    manifest = MANIFEST.format(mime=MIMETYPE, images=manifest_images)
    import io as _io
    buf = _io.BytesIO()
    with zipfile.ZipFile(buf, 'w') as z:
        z.writestr(zipfile.ZipInfo('mimetype'), MIMETYPE, compress_type=zipfile.ZIP_STORED)
        z.writestr('META-INF/manifest.xml', manifest)
        z.writestr('content.xml', content)
        z.writestr('styles.xml', STYLES)
        z.writestr('meta.xml', META)
        for name, data in images.items():
            z.writestr('Pictures/' + name, data)
    return buf.getvalue()


def make_png(name, color, size=(64, 64)):
    img = Image.new('RGB', size, color)
    d = ImageDraw.Draw(img)
    d.rectangle([4, 4, size[0] - 5, size[1] - 5], outline=(255, 255, 255), width=2)
    b = io.BytesIO(); img.save(b, format='PNG'); return name, b.getvalue()


def make_jpeg(name, color, size=(64, 64)):
    img = Image.new('RGB', size, color)
    d = ImageDraw.Draw(img)
    d.ellipse([6, 6, size[0] - 7, size[1] - 7], fill=(255, 255, 255))
    b = io.BytesIO(); img.save(b, format='JPEG', quality=85); return name, b.getvalue()


def text_deck():
    """5 slides, dense text, unicode, multiple paragraphs, varied positions."""
    slides = []
    for i in range(5):
        frames = [
            text_frame(1, 1, 24, 3, ['Text Feature Slide %d' % (i + 1)]),
            text_frame(1, 5, 12, 8, [
                'Paragraph one on slide %d.' % (i + 1),
                'Paragraph two with unicode: café, naïve, 日本語.',
                'The quick brown fox jumps over the lazy dog %d times.' % (i + 1),
            ]),
            text_frame(14, 5, 12, 8, [
                'Right column, slide %d' % (i + 1),
                'Bullet-like line A', 'Bullet-like line B', 'Bullet-like line C',
            ]),
        ]
        slides.append(frames)
    return slides, {}


def images_deck():
    """4 slides with PNG/JPEG images, multiple per slide, plus labels."""
    images = dict([
        make_png('red.png', (200, 30, 30)),
        make_png('green.png', (30, 180, 60)),
        make_png('blue.png', (30, 60, 200)),
        make_jpeg('orange.jpg', (230, 140, 20)),
        make_jpeg('purple.jpg', (140, 40, 200)),
    ])
    slides = [
        [text_frame(1, 1, 20, 2, ['Images Feature - Slide 1']),
         image_frame(2, 4, 8, 8, 'red.png'),
         image_frame(12, 4, 8, 8, 'green.png'),
         text_frame(2, 12, 18, 2, ['Two PNG images side by side'])],
        [text_frame(1, 1, 20, 2, ['Images Feature - Slide 2']),
         image_frame(3, 4, 10, 10, 'blue.png'),
         text_frame(16, 6, 10, 6, ['Blue PNG on the left'])],
        [text_frame(1, 1, 20, 2, ['Images Feature - Slide 3']),
         image_frame(2, 4, 7, 7, 'orange.jpg'),
         image_frame(11, 4, 7, 7, 'purple.jpg'),
         image_frame(20, 4, 7, 7, 'red.png'),
         text_frame(2, 12, 24, 2, ['Three images: two JPEG and one PNG'])],
        [text_frame(1, 1, 20, 2, ['Images Feature - Slide 4']),
         image_frame(10, 4, 10, 10, 'green.png')],
    ]
    return slides, images


def mixed_deck():
    """6 slides mixing text and images in varied layouts."""
    images = dict([
        make_png('m1.png', (60, 120, 200), (80, 80)),
        make_png('m2.png', (200, 90, 60), (80, 80)),
        make_jpeg('m3.jpg', (90, 200, 90), (80, 80)),
    ])
    slides = [
        [text_frame(1, 1, 24, 3, ['Mixed Feature Deck'])],
        [text_frame(1, 2, 12, 10, ['Overview', 'Text on the left', 'Image on the right']),
         image_frame(15, 2, 10, 10, 'm1.png')],
        [image_frame(2, 2, 10, 10, 'm2.png'),
         text_frame(14, 2, 12, 10, ['Image on the left', 'Text on the right'])],
        [text_frame(1, 2, 24, 3, ['Data Slide']),
         text_frame(1, 6, 8, 7, ['Metric A: 42', 'Metric B: 17', 'Metric C: 99']),
         text_frame(10, 6, 8, 7, ['Region 1', 'Region 2', 'Region 3']),
         image_frame(19, 6, 7, 7, 'm3.jpg')],
        [text_frame(2, 3, 24, 4, ['Conclusion', 'Mixed content round-trips cleanly.'])],
        [image_frame(3, 2, 8, 8, 'm1.png'),
         image_frame(12, 2, 8, 8, 'm2.png'),
         image_frame(21, 2, 6, 8, 'm3.jpg'),
         text_frame(3, 11, 24, 3, ['Gallery slide with three images'])],
    ]
    return slides, images


def environment():
    install = ROOT / 'build/install'
    return dict(os.environ,
                QT_QPA_PLATFORM='offscreen', QT_QPA_PLATFORMTHEME='generic',
                QT_LOGGING_RULES='*.debug=false', GSETTINGS_BACKEND='memory',
                LD_LIBRARY_PATH=str(install / 'lib/x86_64-linux-gnu'),
                QT_PLUGIN_PATH=str(install / 'lib/x86_64-linux-gnu/qt6/plugins'),
                XDG_DATA_DIRS=str(install / 'share') + ':/usr/local/share:/usr/share')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output', type=Path, default=DATA)
    out = ap.parse_args().output.resolve()
    out.mkdir(parents=True, exist_ok=True)

    decks = [('features_text', text_deck()),
             ('features_images', images_deck()),
             ('features_mixed', mixed_deck())]

    env = environment()
    for name, (slides, images) in decks:
        odp = out / (name + '.odp')
        ppt = out / (name + '.ppt')
        odp.write_bytes(build_odp(slides, images))
        with (out / (name + '.export.log')).open('w') as log:
            subprocess.run([ROOT / 'build/bin/calligraconverter', '--batch',
                            '--mimetype', 'application/vnd.ms-powerpoint', odp, ppt],
                           env=env, stdout=log, stderr=log, check=True, timeout=180)
        print('generated %-18s %2d slides, %2d images -> %s (%d bytes)'
              % (name, len(slides), len(images), ppt.name, ppt.stat().st_size))


if __name__ == '__main__':
    main()
