#!/usr/bin/env python3
"""Colour regression checks for the DOCX, PPTX and XLSX import filters.

Converts each colour fixture (see make_color_fixtures.py) to ODF with
calligraconverter and checks that the expected colours are present, and that
the XLSX conditional formats survive the import.

    python3 tests/check_colors.py [path/to/calligraconverter]

Colours in KNOWN_GAPS are expected but not produced yet. They are reported
without failing the run, so new regressions still show up as failures.
"""
import colorsys
import os
import re
import subprocess
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
CONVERTER = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', 'build', 'install', 'bin', 'calligraconverter')

THEME_HEX = {'lt2': 'EEECE1', 'accent1': '4F81BD', 'accent2': 'C0504D', 'accent3': '9BBB59'}


def _srgb_to_linear(value):
    value /= 255.0
    return value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4


def _linear_to_srgb(value):
    value = value * 12.92 if value <= 0.0031308 else 1.055 * value ** (1 / 2.4) - 0.055
    return round(max(0.0, min(1.0, value)) * 255)


def docx_modified(hex_colour, tint=None, shade=None):
    """WordprocessingML w:themeTint / w:themeShade: a fraction of the colour in linear light."""
    out = []
    for i in (0, 2, 4):
        linear = _srgb_to_linear(int(hex_colour[i:i + 2], 16))
        if tint is not None:
            linear = 1.0 - (1.0 - linear) * tint
        if shade is not None:
            linear *= shade
        out.append(_linear_to_srgb(linear))
    return '%02X%02X%02X' % tuple(out)


def shifted(hex_colour, amount):
    """Tint (amount > 0) or shade (amount < 0) in HLS, as ECMA-376 and Excel define it."""
    r, g, b = (int(hex_colour[i:i + 2], 16) / 255.0 for i in (0, 2, 4))
    h, l, s = colorsys.rgb_to_hls(r, g, b)
    l = l * (1 + amount) if amount < 0 else l * (1 - amount) + amount
    r, g, b = colorsys.hls_to_rgb(h, l, s)
    return '%02X%02X%02X' % tuple(round(v * 255) for v in (r, g, b))


# Expected colours per fixture. Plain strings are explicit RGB; tuples are
# (base colour, tint) with the tint applied the way Excel and PowerPoint do.
EXPECTED = {
    'docx_11_colors.docx': [
        docx_modified(THEME_HEX['accent2'], tint=0x66 / 255.0),   # accent2 tint 66 text
        docx_modified(THEME_HEX['accent2'], tint=0x80 / 255.0),   # accent2 tint 80 cell fill
        docx_modified(THEME_HEX['accent3'], shade=0xBF / 255.0),  # accent3 darker 25% text and cell
        'FF0000', '00B050', '0070C0',                 # run colours
        'FFFF00', '00FFFF', 'FF00FF',                 # highlights
        'FFC7CE', 'C6EFCE',                           # run shading
        'DDEBF7',                                     # paragraph shading
        'FFC000', '1F4E79', 'D9D9D9', 'E2EFDA', 'F2F2F2',  # table cell fills
        '1F3864',                                     # paragraph text colour
        THEME_HEX['accent1'],                         # accent1 theme text
    ],
    'pptx_11_colors.pptx': [
        'E2EFDA',                                     # slide background
        '1F3864', 'FF0000', '00B050', '0070C0',       # text colours
        'FFC000', '1F4E79',                           # rectangle fills
        'C6EFCE', 'FFC7CE', 'DDEBF7', 'D9D9D9', 'F2F2F2', 'FFFFFF',  # table cell fills
        shifted(THEME_HEX['accent1'], 0.4),           # accent1, lighter 40%
        shifted(THEME_HEX['accent2'], -0.25),         # accent2, darker 25%
        shifted(THEME_HEX['accent1'], 0.6),           # accent1, lighter 60% (rectangle)
    ],
    'xlsx_12_colors.xlsx': [
        'FF0000', '00B050', '0070C0', '1F4E79',       # explicit fills
        'FFFF00',                                     # font colour
        THEME_HEX['accent1'],                         # accent1 theme fill
        shifted(THEME_HEX['accent2'], 0.4),           # accent2, lighter 40%
        shifted(THEME_HEX['accent3'], -0.25),         # accent3, darker 25%
        shifted(THEME_HEX['lt2'], -0.5),              # lt2, darker 50%
        'FFC7CE', 'C6EFCE', 'FFEB9C',                 # conditional format fills
    ],
}

# Expected but not produced yet. Reported, not failed. Empty when every fixture passes.
KNOWN_GAPS = {}

# Minimum number of distinct conditional rules per XLSX fixture (the file has 3).
EXPECTED_RULES = {'xlsx_12_colors.xlsx': 3}


def converter_env():
    prefix = os.path.dirname(os.path.dirname(os.path.abspath(CONVERTER)))
    libdir = os.path.join(prefix, 'lib', 'x86_64-linux-gnu')
    env = dict(os.environ)
    env.update({
        'QT_PLUGIN_PATH': os.path.join(libdir, 'qt6', 'plugins'),
        'LD_LIBRARY_PATH': libdir,
        'XDG_DATA_DIRS': os.path.join(prefix, 'share') + ':/usr/local/share:/usr/share',
        'QT_QPA_PLATFORM': 'offscreen',
        'GSETTINGS_BACKEND': 'memory',
    })
    return env


def convert(src, dst_dir):
    ext = 'odt' if src.endswith('docx') else 'ods' if src.endswith('xlsx') else 'odp'
    out = os.path.join(dst_dir, os.path.splitext(os.path.basename(src))[0] + '.' + ext)
    res = subprocess.run([CONVERTER, '--batch', src, out], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=converter_env())
    if res.returncode != 0 or not os.path.exists(out):
        raise RuntimeError('conversion failed for %s (rc=%d)' % (src, res.returncode))
    return out


def read_output(odf):
    with zipfile.ZipFile(odf) as z:
        xml = (z.read('content.xml') + z.read('styles.xml')).decode('utf-8')
    colours = set(m.upper() for m in re.findall(r'(?:fo:color|fo:background-color|draw:fill-color)="#([0-9a-fA-F]{6})"', xml))
    rules = set(re.findall(r'style:condition="(cell-content[^"]*|is-true-formula[^"]*)"', xml))
    return colours, rules


def has_colour(colours, wanted):
    wr, wg, wb = (int(wanted[i:i + 2], 16) for i in (0, 2, 4))
    for c in colours:
        r, g, b = (int(c[i:i + 2], 16) for i in (0, 2, 4))
        if abs(r - wr) <= 2 and abs(g - wg) <= 2 and abs(b - wb) <= 2:
            return True
    return False


def main():
    failures = []
    gaps = []
    with tempfile.TemporaryDirectory() as tmp:
        for name, expected in EXPECTED.items():
            odf = convert(os.path.join(HERE, name), tmp)
            colours, rules = read_output(odf)
            missing = [c for c in expected if not has_colour(colours, c)]
            known = [c for c in KNOWN_GAPS.get(name, []) if not has_colour(colours, c)]
            print('%-28s %d colours checked, %d missing' % (name, len(expected), len(missing)))
            for c in missing:
                if c in KNOWN_GAPS.get(name, []):
                    continue
                failures.append('%s: colour #%s missing' % (name, c))
            for c in known:
                gaps.append('%s: colour #%s (known gap)' % (name, c))
            if name in EXPECTED_RULES:
                print('%-28s conditional rules %d (want >= %d)' % ('', len(rules), EXPECTED_RULES[name]))
                if len(rules) < EXPECTED_RULES[name]:
                    failures.append('%s: %d conditional rules, want >= %d' % (name, len(rules), EXPECTED_RULES[name]))
    if gaps:
        print('\nKnown gaps (not failing):')
        for g in gaps:
            print(' ', g)
    if failures:
        print('\nFAILED')
        for f in failures:
            print(' ', f)
        return 1
    print('\nall colour checks passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
