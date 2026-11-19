#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Round-trip fidelity and speed benchmark for the three feature .ppt fixtures.

For each of features_text/images/mixed.ppt the harness:

  1. imports  the .ppt to ODP  (``ppttoodp``)        -- timed
  2. exports  that ODP back to .ppt (calligraconverter) -- timed
  3. compares the original .ppt with the round-tripped .ppt by importing both
     and diffing slide count, text, images and frame geometry (fidelity).
  4. edits the imported ODP slightly (one text string + one geometry value),
     exports it to a "saved" .ppt, and compares original vs saved so the edit
     is observable and everything else is shown to survive.

A human-readable report is printed and written to ``data/roundtrip_report.txt``.

Usage:  python3 roundtrip_benchmark.py [--output DIR] [--repeat N]
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import time
import zipfile

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
DATA = HERE / 'data'
WORK = DATA / 'roundtrip_work'

DRAW = 'urn:oasis:names:tc:opendocument:xmlns:drawing:1.0'
SVG = 'urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0'
TEXT = 'urn:oasis:names:tc:opendocument:xmlns:text:1.0'

PPT_MIME = 'application/vnd.ms-powerpoint'


def environment():
    install = ROOT / 'build/install'
    return dict(os.environ,
                QT_QPA_PLATFORM='offscreen', QT_QPA_PLATFORMTHEME='generic',
                QT_LOGGING_RULES='*.debug=false', GSETTINGS_BACKEND='memory',
                LD_LIBRARY_PATH=str(install / 'lib/x86_64-linux-gnu'),
                QT_PLUGIN_PATH=str(install / 'lib/x86_64-linux-gnu/qt6/plugins'),
                XDG_DATA_DIRS=str(install / 'share') + ':/usr/local/share:/usr/share')


def run(cmd, log):
    with open(log, 'w') as fh:
        subprocess.run(cmd, env=environment(), stdout=fh, stderr=fh,
                       check=True, timeout=180)


def timed_import(ppt, odp, log):
    t0 = time.perf_counter()
    run([ROOT / 'build/bin/ppttoodp', ppt, odp], log)
    return time.perf_counter() - t0


def timed_export(odp, ppt, log):
    t0 = time.perf_counter()
    run([ROOT / 'build/bin/calligraconverter', '--batch',
         '--mimetype', PPT_MIME, odp, ppt], log)
    return time.perf_counter() - t0


def parse_errors(log):
    txt = Path(log).read_text(errors='replace')
    return [line for line in txt.splitlines()
            if re.search(r'error parsing PowerPointStructs|bytes left at the end', line, re.I)]


def content(odp):
    with zipfile.ZipFile(odp) as z:
        return z.read('content.xml').decode('utf-8')


def metrics(odp):
    """Extract comparable fidelity metrics from an ODP."""
    s = content(odp)
    pages = s.count('<draw:page ')
    texts = [re.sub(r'<[^>]+>', '', t).strip()
             for t in re.findall(r'<text:p[^>]*>(.*?)</text:p>', s, re.S)]
    texts = [t for t in texts if t]
    images = s.count('<draw:image ')
    # frame geometry in cm, rounded to 0.01 cm to ignore float noise
    frames = []
    for m in re.finditer(r'<draw:frame\b([^>]*)>', s):
        attrs = m.group(1)
        def val(a):
            mm = re.search(r'%s="([-\d.]+)(mm|cm)"' % a, attrs)
            if not mm:
                return None
            v = float(mm.group(1))
            return round(v if mm.group(2) == 'cm' else v / 10.0, 2)
        geom = (val('svg:x'), val('svg:y'), val('svg:width'), val('svg:height'))
        if any(g is not None for g in geom):
            frames.append(geom)
    return {'pages': pages, 'texts': texts, 'images': images, 'frames': frames}


def text_diff(a, b):
    """Return (missing, added) text lists between two metric dicts."""
    sa, sb = set(a['texts']), set(b['texts'])
    return sorted(sa - sb), sorted(sb - sa)


def geom_drift(a, b):
    """Max per-coordinate drift (cm) over the common number of frames."""
    worst = 0.0
    for fa, fb in zip(a['frames'], b['frames']):
        for va, vb in zip(fa, fb):
            if va is not None and vb is not None:
                worst = max(worst, abs(va - vb))
    return round(worst, 3)


def edit_odp(src, dst, old_text, new_text, old_x, new_x):
    """Slightly edit an ODP: change one text string and one frame x position."""
    s = content(src)
    assert old_text in s, 'edit target text not found: %s' % old_text
    s = s.replace(old_text, new_text, 1)
    assert old_x in s, 'edit target geometry not found: %s' % old_x
    s = s.replace(old_x, new_x, 1)
    with zipfile.ZipFile(src) as zin, zipfile.ZipFile(dst, 'w') as zout:
        for item in zin.infolist():
            data = s.encode('utf-8') if item.filename == 'content.xml' else zin.read(item.filename)
            zout.writestr(item, data)


def first_text(odp):
    m = metrics(odp)
    return m['texts'][0] if m['texts'] else None


def first_x(odp):
    s = content(odp)
    m = re.search(r'<draw:frame\b[^>]*svg:x="([-\d.]+(?:cm|mm))"', s)
    return m.group(1) if m else None


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output', type=Path, default=DATA)
    ap.add_argument('--repeat', type=int, default=1,
                    help='repeat the round-trip N times and report averages')
    args = ap.parse_args()
    data = args.output.resolve()
    work = data / 'roundtrip_work'
    work.mkdir(parents=True, exist_ok=True)
    env = environment()

    report = []
    report.append('Native binary .ppt round-trip benchmark')
    report.append('=' * 60)
    report.append('')

    fixtures = ['features_text', 'features_images', 'features_mixed']
    for name in fixtures:
        original = data / (name + '.ppt')
        if not original.exists():
            raise SystemExit('missing fixture: %s' % original)

        times = []
        last_rt_odp = last_rt_ppt = None
        last_import_errors = last_rt_errors = []
        for rep in range(args.repeat):
            base = work / ('%s_r%d' % (name, rep))
            imp_odp = base.with_suffix('.imported.odp')
            rt_ppt = base.with_suffix('.roundtrip.ppt')
            rt_odp = base.with_suffix('.roundtrip.odp')
            imp_log = base.with_suffix('.import.log')
            exp_log = base.with_suffix('.export.log')
            rt_imp_log = base.with_suffix('.roundtrip_import.log')

            t_import = timed_import(original, imp_odp, imp_log)
            t_export = timed_export(imp_odp, rt_ppt, exp_log)
            t_back = timed_import(rt_ppt, rt_odp, rt_imp_log)
            times.append((t_import, t_export, t_back))
            last_rt_odp, last_rt_ppt = rt_odp, rt_ppt
            last_import_errors = parse_errors(imp_log)
            last_rt_errors = parse_errors(rt_imp_log)

        # average timings
        n = len(times)
        ti = sum(t[0] for t in times) / n
        te = sum(t[1] for t in times) / n
        tb = sum(t[2] for t in times) / n
        total = sum(sum(t) for t in times) / n

        orig_m = metrics(last_rt_odp.parent / (name + '_r0.imported.odp'))
        rt_m = metrics(last_rt_odp)

        report.append('[%s]' % name)
        report.append('  speed (avg over %d run(s)):' % n)
        report.append('    ppt -> odp (import)      : %6.3f s' % ti)
        report.append('    odp -> ppt (export)      : %6.3f s' % te)
        report.append('    ppt -> odp (re-import)   : %6.3f s' % tb)
        report.append('    full round-trip          : %6.3f s' % total)
        report.append('  fidelity (original vs round-tripped):')
        report.append('    slides    : %d -> %d' % (orig_m['pages'], rt_m['pages']))
        report.append('    texts     : %d -> %d' % (len(orig_m['texts']), len(rt_m['texts'])))
        report.append('    images    : %d -> %d' % (orig_m['images'], rt_m['images']))
        missing, added = text_diff(orig_m, rt_m)
        report.append('    text lost : %s' % (missing if missing else 'none'))
        report.append('    text added: %s' % (added if added else 'none'))
        report.append('    max geom drift: %.3f cm' % geom_drift(orig_m, rt_m))
        report.append('    parse errors: %s' % (len(last_import_errors) + len(last_rt_errors)))

        # ---- modify -> save -> compare ----
        imp_odp = work / (name + '_r0.imported.odp')
        old_text = first_text(imp_odp)
        old_x = first_x(imp_odp)
        new_text = old_text + ' EDITED'
        new_x = '1.5cm'
        edited_odp = work / (name + '.edited.odp')
        saved_ppt = data / (name + '_edited.ppt')
        edit_odp(imp_odp, edited_odp, old_text, new_text, old_x, new_x)
        timed_export(edited_odp, saved_ppt, work / (name + '.edited_export.log'))
        saved_odp = work / (name + '.edited_back.odp')
        timed_import(saved_ppt, saved_odp, work / (name + '.edited_back.log'))
        saved_m = metrics(saved_odp)

        report.append('  modify -> save -> compare:')
        report.append('    edit: %r -> %r' % (old_text, new_text))
        report.append('    edit: frame x %s -> %s' % (old_x, new_x))
        report.append('    edited text present in saved file : %s'
                      % (new_text in saved_m['texts']))
        report.append('    original text absent in saved file: %s'
                      % (old_text not in saved_m['texts']))
        report.append('    slides %d -> %d, images %d -> %d'
                      % (orig_m['pages'], saved_m['pages'], orig_m['images'], saved_m['images']))
        report.append('')

    text = '\n'.join(report)
    print(text)
    (data / 'roundtrip_report.txt').write_text(text)


if __name__ == '__main__':
    main()
