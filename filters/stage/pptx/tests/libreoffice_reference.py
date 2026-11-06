# SPDX-License-Identifier: LGPL-2.0-or-later
"""Run the installed LibreOffice engine in an isolated reference process."""
import ctypes as C, os, pathlib, sys, tempfile
os.environ['SAL_USE_VCLPLUGIN']='svp'
os.environ['GSETTINGS_BACKEND']='memory'
os.environ['SAL_DISABLE_OPENCL']='1'
class Obj(C.Structure): pass
class Doc(C.Structure): pass
P=C.POINTER(Obj); D=C.POINTER(Doc)
class OfficeClass(C.Structure):
 _fields_=[('size',C.c_size_t),('destroy',C.CFUNCTYPE(None,P)),('load',C.CFUNCTYPE(D,P,C.c_char_p)),('error',C.CFUNCTYPE(C.c_void_p,P)),('loadopts',C.CFUNCTYPE(D,P,C.c_char_p,C.c_char_p)),('free',C.CFUNCTYPE(None,C.c_void_p))]
class DocClass(C.Structure):
 _fields_=[('size',C.c_size_t),('destroy',C.CFUNCTYPE(None,D)),('save',C.CFUNCTYPE(C.c_int,D,C.c_char_p,C.c_char_p,C.c_char_p))]
Obj._fields_=[('cls',C.POINTER(OfficeClass))]; Doc._fields_=[('cls',C.POINTER(DocClass))]
lib=C.CDLL('/usr/lib/libreoffice/program/libmergedlo.so',mode=C.RTLD_GLOBAL)
lib.libreofficekit_hook_2.argtypes=[C.c_char_p,C.c_char_p];lib.libreofficekit_hook_2.restype=P
with tempfile.TemporaryDirectory(prefix='stage-lok-', ignore_cleanup_errors=True) as profile:
 office=lib.libreofficekit_hook_2(b'/usr/lib/libreoffice/program',pathlib.Path(profile).as_uri().encode())
 if not office: raise RuntimeError('LO init failed')
 doc=office.contents.cls.contents.load(office,pathlib.Path(sys.argv[1]).resolve().as_uri().encode())
 if not doc: raise RuntimeError('LO document load failed')
 for output in sys.argv[2:]:
  path=pathlib.Path(output).resolve()
  options=b'{"ExportHiddenSlides":{"type":"boolean","value":"true"}}' if path.suffix.lower()=='.pdf' else None
  ok=doc.contents.cls.contents.save(doc,path.as_uri().encode(),path.suffix[1:].encode(),options)
  print('SAVE',path,ok,flush=True)
  if not ok: raise RuntimeError('LO save failed')
 # Process isolation also avoids image/media backend disposal deadlocks.
sys.stdout.flush()
os._exit(0)
