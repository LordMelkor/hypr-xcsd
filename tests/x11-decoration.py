"""Run only inside a disposable compositor: XCSD_DISPOSABLE=1 python3 this.py."""
import ctypes as C
import json
import os
from pathlib import Path
import subprocess
import time
assert os.environ.get('XCSD_DISPOSABLE') == '1', 'disposable session required'
lib=C.CDLL('libX11.so.6')
def api(name, restype, args):
 f=getattr(lib,name);f.restype=restype;f.argtypes=args;return f
ptr=C.c_void_p;ul=C.c_ulong;integer=C.c_int
open_display=api('XOpenDisplay',ptr,[C.c_char_p]);d=open_display(None);assert d
root=api('XDefaultRootWindow',ul,[ptr])(d)
win=api('XCreateSimpleWindow',ul,[ptr,ul,integer,integer,C.c_uint,C.c_uint,C.c_uint,ul,ul])(d,root,200,200,500,300,0,0,0)
atom=lambda n:api('XInternAtom',ul,[ptr,C.c_char_p,integer])(d,n.encode(),0)
name=api('XStoreName',integer,[ptr,ul,C.c_char_p]);name(d,win,b'xcsd isolated policy test')
mapwin=api('XMapWindow',integer,[ptr,ul]);unmap=api('XUnmapWindow',integer,[ptr,ul])
sync=lambda:api('XSync',integer,[ptr,integer])(d,0)
change=api('XChangeProperty',integer,[ptr,ul,ul,ul,integer,integer,ptr,integer])
delete=api('XDeleteProperty',integer,[ptr,ul,ul])
motif=atom('_MOTIF_WM_HINTS')
# PID lets the compositor attribute the fixture; the test locates it by title.
pid=(ul*1)(os.getpid());change(d,win,atom('_NET_WM_PID'),atom('CARDINAL'),32,0,C.cast(pid,ptr),1)
mapwin(d,win);sync()
def ctl(*args):return subprocess.check_output(['hyprctl',*args],text=True)
def client():return next((w for w in json.loads(ctl('clients','-j')) if w.get('title')=='xcsd isolated policy test'),None)
def expect(tag, label):
 for _ in range(100):
  w=client()
  if w and ('x11-csd' in w['tags'])==tag:
   results.append(label);return
  time.sleep(.02)
 raise AssertionError((label, w))
def hint(fields, fmt=32):
 values=(ul*len(fields))(*fields)
 change(d,win,motif,motif,fmt,0,C.cast(values,ptr),len(fields));sync()
results=[]
try:
 expect(False,'absent hint keeps server decorations')
 # A rejected resize request is not decoration evidence.
 class Client(C.Structure):
  _fields_=[('type',integer),('serial',ul),('send_event',integer),('display',ptr),('window',ul),('message_type',ul),('format',integer),('data',C.c_long*5)]
 class Event(C.Union):_fields_=[('xclient',Client),('pad',C.c_long*24)]
 e=Event();e.xclient.type=33;e.xclient.display=d;e.xclient.window=win;e.xclient.message_type=atom('_NET_WM_MOVERESIZE');e.xclient.format=32;e.xclient.data[:]=[200,200,4,1,1]
 api('XSendEvent',integer,[ptr,ul,integer,C.c_long,C.POINTER(Event)])(d,root,0,0x180000,C.byref(e));sync();time.sleep(.1)
 expect(False,'resize grip without a hint never suppresses the bar')
 hint([2,0,0,0,0]);expect(True,'valid no-decoration hint suppresses bar')
 hint([2,0,1,0,0]);expect(False,'explicit decoration hint restores bar')
 hint([2,0,0,0,0]);expect(True,'no-decoration hint can be restored')
 delete(d,win,motif);sync();expect(False,'deleted hint restores bar')
 hint([2,0,0,0,0]);expect(True,'setup malformed transition')
 hint([2]);expect(False,'short hint restores bar')
 hint([2,0,0,0,0]);expect(True,'setup wrong-format transition')
 hint([2],8);expect(False,'wrong format restores bar')
 unmap(d,win);sync();mapwin(d,win);sync();expect(False,'remap does not revive stale evidence')
 ctl('eval','assert(hl.plugin.xcsd.drag_active() == false)')
 if os.environ.get('XCSD_INIT_FAIL_SO'):
  hint([2,0,0,0,0]);expect(True,'setup tagged window before unload')
  ctl('plugin','unload',os.environ['XCSD_SO']);expect(False,'unload restores server bar')
  failed=subprocess.run(['hyprctl','plugin','load',os.environ['XCSD_INIT_FAIL_SO']],text=True,capture_output=True)
  assert 'injected failure after tag mutations' in failed.stdout, failed.stdout
  assert not any(p['name']=='hypr-xcsd' for p in json.loads(ctl('plugin','list','-j')))
  expect(False,'failed init rolls back tag mutations')
  ctl('plugin','load',os.environ['XCSD_SO']);expect(True,'clean load recovers after failed init')
 print(json.dumps({'passed':results,'native_drag_query':False},indent=2),flush=True)
finally:
 api('XDestroyWindow',integer,[ptr,ul])(d,win);sync();api('XCloseDisplay',integer,[ptr])(d)
