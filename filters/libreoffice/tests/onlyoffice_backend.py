#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Test the x2t adapter contract with a controlled executable, not the real engine."""
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import zipfile

REPO = Path(__file__).resolve().parents[3]
WORKER = REPO/'build/bin/calligraofficeconverter'


def package(path, fmt):
    with zipfile.ZipFile(path, 'w') as z:
        if fmt == 'pptx':
            z.writestr('ppt/presentation.xml', '<p:presentation xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main"/>')
            z.writestr('[Content_Types].xml', '<Types/>')
            z.writestr('_rels/.rels', '<Relationships/>')
        else:
            z.writestr('content.xml', '<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"><office:body><office:presentation/></office:body></office:document-content>')
            z.writestr('META-INF/manifest.xml', '<manifest/>')


def main():
    with tempfile.TemporaryDirectory(prefix='onlyoffice-adapter-') as temp:
        directory = Path(temp)/'spaces & symbols'
        directory.mkdir()
        stub = directory/'x2t'
        stub.write_text('#!'+sys.executable+'\n'+'''import hashlib,json,os,shutil,sys,xml.etree.ElementTree as E
p={n.tag:n.text for n in E.parse(sys.argv[1]).getroot()}
p['input_sha256']=hashlib.sha256(open(p['m_sFileFrom'],'rb').read()).hexdigest()
json.dump(p,open(os.environ['X2T_TEST_LOG'],'w'))
mode=os.environ.get('X2T_TEST_MODE','success')
if mode=='failure':
 print('intentional x2t failure',file=sys.stderr);sys.exit(23)
if mode=='crash':
 os.kill(os.getpid(),15)
if mode=='invalid':
 open(p['m_sFileTo'],'wb').write(b'bad package')
elif mode!='no-output':
 shutil.copyfile(os.environ['X2T_TEST_RESULT'],p['m_sFileTo'])
''')
        stub.chmod(0o755)
        source, odp = directory/'source.pptx', directory/'fixture.odp'
        package(source, 'pptx'); package(odp, 'odp')
        original = hashlib.sha256(source.read_bytes()).hexdigest()
        log, destination = directory/'task.json', directory/'output.odp'
        env = dict(os.environ, CALLIGRA_PPTX_BACKEND='onlyoffice', CALLIGRA_ONLYOFFICE_X2T=str(stub),
                   X2T_TEST_LOG=str(log), X2T_TEST_RESULT=str(odp))
        args = [str(WORKER), '--input', str(source), '--output', str(destination), '--from', 'pptx', '--to', 'odp']
        run = lambda extra=(), changes={}: subprocess.run(args+list(extra),env=dict(env,**changes),stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=20)
        result = run(); assert result.returncode == 0, result.stderr
        task = json.loads(log.read_text())
        assert task['m_nFormatTo'] == '131'
        assert task['input_sha256'] == original, 'ONLYOFFICE must receive the original PPTX, without LibreOffice-specific rewrites'
        assert destination.read_bytes() == odp.read_bytes()
        assert hashlib.sha256(source.read_bytes()).hexdigest() == original
        # Explicit CLI backend takes precedence over an invalid environment setting.
        assert run(['--engine','onlyoffice'], {'CALLIGRA_PPTX_BACKEND':'invalid'}).returncode == 0
        for mode in ['failure','crash','invalid','no-output']:
            destination.write_bytes(b'KEEP')
            result = run(changes={'X2T_TEST_MODE':mode})
            assert result.returncode != 0, mode
            assert destination.read_bytes() == b'KEEP', mode
            if mode == 'failure': assert b'intentional x2t failure' in result.stderr
        for extra, changes in [([], {'CALLIGRA_ONLYOFFICE_X2T':str(directory/'missing')}),
                               (['--onlyoffice-x2t',str(directory/'missing')], {})]:
            result = run(extra, changes)
            assert result.returncode == 78, result.stderr
            assert b'ONLYOFFICE x2t is not available' in result.stderr
            assert destination.read_bytes() == b'KEEP'
        # Reverse conversion uses the PPTX format code and the same validation.
        env['X2T_TEST_RESULT'] = str(source)
        result = subprocess.run([str(WORKER),'--engine','onlyoffice','--input',str(odp),'--output',str(directory/'reverse.pptx'),
                                '--from','odp','--to','pptx'],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=20)
        assert result.returncode == 0, result.stderr
        assert json.loads(log.read_text())['m_nFormatTo'] == '129'
        print('PASS: x2t task XML, escaped paths, format IDs, untouched input, selection precedence, failures, crashes, missing engine and atomic output')
        print('Real ONLYOFFICE conversion is a separate integration test; this test uses a stub.')


if __name__ == '__main__': main()
