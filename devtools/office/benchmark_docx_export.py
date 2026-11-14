#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Measure fresh-process native and LibreOffice DOCX export from the same ODT."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import statistics
import subprocess
import time

ROOT=Path(__file__).resolve().parents[2]
REFERENCE=ROOT/'filters/stage/pptx/tests/libreoffice_reference.py'


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('input',type=Path)
    parser.add_argument('--output',type=Path,required=True);parser.add_argument('--repeat',type=int,default=3)
    args=parser.parse_args()
    if args.repeat<1:parser.error('repeat must be positive')
    source=args.input.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    spec=importlib.util.spec_from_file_location('options',ROOT/'filters/words/docx/export/tests/export_options.py')
    options=importlib.util.module_from_spec(spec);spec.loader.exec_module(options);env=options.environment()
    before=digest(source)
    report=dict(source=str(source),source_sha256=before,repeats=args.repeat,engines={},
        timing_scope='Fresh processes, including startup and file I/O; filesystem cache is warm. All samples run sequentially.',
        reference_version=subprocess.check_output(['/usr/bin/libreoffice','--version'],env=env,text=True).strip(),
        native_filter_sha256=digest(ROOT/'build/install/lib/x86_64-linux-gnu/qt6/plugins/calligra/formatfilters/calligra_filter_odt2docx.so'))
    cache=(ROOT/'build/CMakeCache.txt').read_text()
    report['build_type']=next((line.split('=',1)[1] for line in cache.splitlines() if line.startswith('CMAKE_BUILD_TYPE:')),'unknown')
    for engine in ['native','libreoffice']:
        samples=[]
        for index in range(args.repeat):
            directory=out/engine/str(index+1);directory.mkdir(parents=True)
            target=directory/'output.docx';metrics=directory/'resources.txt'
            command=[ROOT/'build/bin/calligraconverter','--batch',source,target] if engine=='native' else ['/usr/bin/python3',REFERENCE,source,target]
            start=time.perf_counter()
            with (directory/'conversion.log').open('wb') as log:
                process=subprocess.run(['/usr/bin/time','-f','%U %S %M','-o',metrics,*command],env=env,stdout=log,stderr=log,timeout=180)
            sample=dict(seconds=time.perf_counter()-start,exit=process.returncode)
            if process.returncode or not target.is_file():sample['error']='Conversion failed or produced no file'
            else:
                user,system,memory=metrics.read_text().splitlines()[-1].split()
                sample.update(cpu_seconds=float(user)+float(system),max_rss_kib=int(memory),output_bytes=target.stat().st_size)
            samples.append(sample)
        valid=all('error' not in sample for sample in samples)
        report['engines'][engine]=dict(samples=samples,
            median_seconds=statistics.median(s['seconds'] for s in samples) if valid else None,
            median_max_rss_mib=statistics.median(s['max_rss_kib'] for s in samples)/1024 if valid else None)
    report['source_unchanged']=digest(source)==before
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    lines=['# Native DOCX export performance','',report['reference_version'],'',
        f"Build: {report['build_type']}. Runs per engine: {args.repeat}.",report['timing_scope'],'',
        '| Engine | Median seconds | Median peak MiB |','|---|---:|---:|']
    for engine,value in report['engines'].items():
        if value['median_seconds'] is None:lines.append(f'| {engine} | FAIL | FAIL |')
        else:lines.append(f"| {engine} | {value['median_seconds']:.3f} | {value['median_max_rss_mib']:.1f} |")
    lines += ['',f"Source unchanged: {report['source_unchanged']}.",'This measures conversion cost, not formatting fidelity. See the separate export comparison report.']
    (out/'REPORT.md').write_text('\n'.join(lines)+'\n');print(out/'REPORT.md',flush=True)
    return 0 if report['source_unchanged'] and all(v['median_seconds'] is not None for v in report['engines'].values()) else 1


if __name__=='__main__':raise SystemExit(main())
