#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Compare native and LibreOffice PPTX exports from exactly the same ODP."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import zipfile
import fitz
from PIL import Image, ImageChops, ImageStat

ROOT = Path(__file__).resolve().parents[2]
TESTS = ROOT/'filters/stage/pptx/tests'


def module(name, path):
    spec = importlib.util.spec_from_file_location(name,path)
    result = importlib.util.module_from_spec(spec);spec.loader.exec_module(result)
    return result


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--baseline',type=Path,help='Earlier native export of this same ODP')
    args = parser.parse_args();source=args.input.resolve();out=args.output.resolve()
    out.mkdir(parents=True,exist_ok=False)
    env = module('export_options',TESTS/'export_options_regressions.py').environment()
    compat = module('pptx_compat',TESTS/'compatibility.py')
    before = digest(source)

    def run(command, name):
        with (out/(name+'.log')).open('wb') as log:
            subprocess.run(command,env=env,stdout=log,stderr=log,check=True,timeout=300)

    run([ROOT/'build/bin/calligraconverter','--batch',source,out/'native.pptx'],'native-export')
    run(['/usr/bin/python3',TESTS/'libreoffice_reference.py',source,out/'reference.pptx'],'reference-export')
    run(['/usr/bin/python3',TESTS/'libreoffice_reference.py',out/'reference.pptx',out/'reference.pdf'],'reference-reopen')
    run(['/usr/bin/python3',TESTS/'libreoffice_reference.py',out/'native.pptx',out/'native.pdf'],'native-reopen')
    if args.baseline:
        run(['/usr/bin/python3',TESTS/'libreoffice_reference.py',args.baseline.resolve(),out/'baseline.pdf'],'baseline-reopen')
    actual, reference = compat.features(out/'native.pptx'), compat.features(out/'reference.pptx')
    checks = {key: actual[key]==reference[key] for key in ['slides','slide_size','tables','charts','hidden','media','table_cells','chart_values']}
    checks['text'] = {i:compat.normalize(t) for i,t in actual['slide_texts'].items()} == {i:compat.normalize(t) for i,t in reference['slide_texts'].items()}
    checks['notes'] = compat.normalize(actual['notes'])==compat.normalize(reference['notes'])
    checks['hyperlinks'] = set(actual['links'])==set(reference['links'])

    def compare_pdf(name):
        expected_doc, actual_doc = fitz.open(out/'reference.pdf'), fitz.open(out/(name+'.pdf'))
        visual=[]
        for index in range(min(len(expected_doc),len(actual_doc))):
            expected_pix=expected_doc[index].get_pixmap(alpha=False);actual_pix=actual_doc[index].get_pixmap(alpha=False)
            expected=Image.frombytes('RGB',(expected_pix.width,expected_pix.height),expected_pix.samples)
            actual_image=Image.frombytes('RGB',(actual_pix.width,actual_pix.height),actual_pix.samples).resize(expected.size)
            difference=ImageChops.difference(expected,actual_image)
            score=1-sum(ImageStat.Stat(difference).mean)/(3*255)
            visual.append(dict(slide=index+1,pixel_similarity=score))
            actual_image.save(out/f'{name}-{index+1}.png');difference.save(out/f'{name}-difference-{index+1}.png')
        return dict(pages=len(actual_doc),reference_pages=len(expected_doc),slides=visual,
                    passed=len(actual_doc)==len(expected_doc)==reference['slides'] and all(s['pixel_similarity']>=.92 for s in visual))

    visual = compare_pdf('native');checks['rendering'] = visual['passed']
    integrity={}
    for name in ['native','reference']:
        with zipfile.ZipFile(out/(name+'.pptx')) as package:
            errors=[]
            for part in package.namelist():
                if part.endswith(('.xml','.rels')):
                    try:compat.ET.fromstring(package.read(part))
                    except compat.ET.XMLSyntaxError as error:errors.append(dict(part=part,error=str(error)))
            integrity[name]=dict(crc_pass=package.testzip() is None,xml_errors=errors)
    checks['package_integrity'] = all(r['crc_pass'] and not r['xml_errors'] for r in integrity.values())
    checks['source_unchanged'] = digest(source)==before
    report=dict(source=str(source),source_sha256=before,checks=checks,passed=all(checks.values()),
                native_features=actual,reference_features=reference,visual=visual,package_integrity=integrity)
    report['native_filter_sha256']=digest(ROOT/'build/install/lib/x86_64-linux-gnu/qt6/plugins/calligra/formatfilters/calligra_filter_odp2pptx.so')
    report['flake_sha256']=digest(ROOT/'build/bin/libflake.so')
    if args.baseline:report['baseline_visual']=compare_pdf('baseline')
    report['reference_version']=subprocess.check_output(['/usr/bin/libreoffice','--version'],env=env,text=True).strip()
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    lines=['# Native PPTX export comparison','',report['reference_version'],'',
           'Both exporters read the same ODP. Native and reference PPTX are rendered by LibreOffice.','',
           '| Check | Result |','|---|---|']
    lines += [f"| {key} | {'PASS' if value else 'FAIL'} |" for key,value in checks.items()]
    lines += ['', '| Slide | Previous native similarity | Updated native similarity |','|---|---:|---:|']
    baseline=report.get('baseline_visual',{}).get('slides',[])
    for index, slide in enumerate(visual['slides']):
        old=f"{baseline[index]['pixel_similarity']:.5f}" if index<len(baseline) else 'not measured'
        lines.append(f"| {index+1} | {old} | {slide['pixel_similarity']:.5f} |")
    lines += ['', 'Per-slide pixel similarity is diagnostic. The rendering gate requires every slide to score at least 0.92; it does not prove exact visual identity.','',
              'Source SHA-256: `'+before+'`. Raw feature checks, difference images and conversion logs are saved alongside this report.']
    (out/'REPORT.md').write_text('\n'.join(lines)+'\n')
    print(out/'REPORT.md',flush=True)
    return 0 if report['passed'] else 1


if __name__ == '__main__':raise SystemExit(main())
