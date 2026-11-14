#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Verify native PPTX import/export with office backends deliberately disabled."""
import argparse
import hashlib
import json
import subprocess
import zipfile
from pathlib import Path
from lxml import etree as E
from compatibility import features, normalize
from export_options_regressions import environment

ROOT = Path(__file__).resolve().parents[4]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source, out = args.input.resolve(), args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    before = hashlib.sha256(source.read_bytes()).hexdigest()
    env = environment()
    env.update(PATH='/nonexistent', CALLIGRA_OFFICE_BACKEND='disabled',
               CALLIGRA_PPTX_BACKEND='disabled', CALLIGRA_LIBREOFFICE_PROGRAM='/nonexistent',
               CALLIGRA_ONLYOFFICE_X2T='/nonexistent')
    converter = ROOT/'build/bin/calligraconverter'
    for src, target in [(source, out/'native.odp'), (out/'native.odp', out/'native.pptx')]:
        with (out/(target.suffix[1:]+'.log')).open('w') as log:
            subprocess.run([converter, '--batch', src, target], env=env,
                           stdout=log, stderr=log, check=True, timeout=180)
        with zipfile.ZipFile(target) as package:
            assert package.testzip() is None
            for name in package.namelist():
                if name.endswith(('.xml', '.rels')): E.fromstring(package.read(name))
    original, actual = features(source), features(out/'native.pptx')
    checks = {key: original[key] == actual[key] for key in ['slides', 'slide_size', 'table_cells', 'chart_values']}
    checks['slide_texts'] = {i:normalize(t) for i,t in original['slide_texts'].items()} == {i:normalize(t) for i,t in actual['slide_texts'].items()}
    checks['notes'] = normalize(original['notes']) == normalize(actual['notes'])
    checks['source_unchanged'] = hashlib.sha256(source.read_bytes()).hexdigest() == before
    dependencies = {}
    plugins = ROOT/'build/install/lib/x86_64-linux-gnu/qt6/plugins/calligra/formatfilters'
    for name in ['calligra_filter_pptx2odp.so', 'calligra_filter_odp2pptx.so']:
        output = subprocess.check_output(['/usr/bin/ldd', plugins/name], text=True, env=environment())
        dependencies[name] = output
        checks[name+'_no_office_link'] = all(token not in output.lower() for token in ['libreoffice', 'mergedlo', 'onlyoffice'])
    report = dict(source=str(source), source_sha256=before, checks=checks,
                  passed=all(checks.values()), dynamic_dependencies=dependencies)
    (out/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    assert report['passed'], checks
    print('PASS: native import/export, text/notes/data, valid packages and no office runtime links; backends disabled and PATH empty')


if __name__ == '__main__':
    main()
