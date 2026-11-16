"""Maximize gate for x11-csd windows. Run only inside a disposable compositor.

The compositor's config must suppress maximize for every window and float the
fixture, as Omarchy does:

  hl.window_rule({ name = "suppress", match = { class = ".*" }, suppress_event = "maximize" })
  hl.window_rule({ name = "fixture-float", match = { title = "^xcsd max.*" }, float = true })

Without the suppress rule Hyprland handles the requests itself and this test
proves nothing. It needs real pointer clicks: the fixture acts like an app's
maximize button and sends _NET_WM_STATE when it sees a button release. Each
step that waits writes "WAIT <n> <what>" to $XCSD_STATUS (and the window
title); click once inside the window. "DONE" or "FAIL ..." ends the run.

  XCSD_DISPOSABLE=1 XCSD_STATUS=/path/status python3 tests/x11-maximize.py
"""
import ctypes as C
import json
import os
import subprocess
import time

assert os.environ.get('XCSD_DISPOSABLE') == '1', 'disposable session required'
STATUS = os.environ.get('XCSD_STATUS')
lib = C.CDLL('libX11.so.6')
ptr, ul, integer = C.c_void_p, C.c_ulong, C.c_int


def api(name, restype, args):
    f = getattr(lib, name)
    f.restype = restype
    f.argtypes = args
    return f


open_display = api('XOpenDisplay', ptr, [C.c_char_p])
for _ in range(200):
    d = open_display(None)
    if d:
        break
    time.sleep(.05)
assert d, 'no X display'
root = api('XDefaultRootWindow', ul, [ptr])(d)
create = api('XCreateSimpleWindow', ul, [ptr, ul, integer, integer, C.c_uint, C.c_uint, C.c_uint, ul, ul])
atom = lambda n: api('XInternAtom', ul, [ptr, C.c_char_p, integer])(d, n.encode(), 0)
store_name = api('XStoreName', integer, [ptr, ul, C.c_char_p])
mapwin = api('XMapWindow', integer, [ptr, ul])
destroy = api('XDestroyWindow', integer, [ptr, ul])
select_input = api('XSelectInput', integer, [ptr, ul, C.c_long])
pending = api('XPending', integer, [ptr])
sync = lambda: api('XSync', integer, [ptr, integer])(d, 0)
change = api('XChangeProperty', integer, [ptr, ul, ul, ul, integer, integer, ptr, integer])
delete = api('XDeleteProperty', integer, [ptr, ul, ul])
get_prop = api('XGetWindowProperty', integer, [ptr, ul, ul, C.c_long, C.c_long, integer, ul, C.POINTER(ul),
                                               C.POINTER(integer), C.POINTER(ul), C.POINTER(ul), C.POINTER(ptr)])
xfree = api('XFree', integer, [ptr])
MOTIF, STATE = atom('_MOTIF_WM_HINTS'), atom('_NET_WM_STATE')
VERT, HORZ = atom('_NET_WM_STATE_MAXIMIZED_VERT'), atom('_NET_WM_STATE_MAXIMIZED_HORZ')


class Client(C.Structure):
    _fields_ = [('type', integer), ('serial', ul), ('send_event', integer), ('display', ptr), ('window', ul),
                ('message_type', ul), ('format', integer), ('data', C.c_long * 5)]


class Event(C.Union):
    _fields_ = [('type', integer), ('xclient', Client), ('pad', C.c_long * 24)]


next_event = api('XNextEvent', integer, [ptr, C.POINTER(Event)])
send_event = api('XSendEvent', integer, [ptr, ul, integer, C.c_long, C.POINTER(Event)])


def status(line):
    if STATUS:
        with open(STATUS, 'a') as f:
            f.write(line + '\n')
    print(line, flush=True)


def ctl(*args):
    return subprocess.check_output(['hyprctl', *args], text=True)


def make_window(title, no_decorations=True, premaximized=False):
    w = create(d, root, 200, 200, 400, 250, 0, 0, 0xffffff)
    store_name(d, w, title.encode())
    if no_decorations:
        set_motif(w)
    if premaximized:
        values = (ul * 2)(VERT, HORZ)
        change(d, w, STATE, atom('ATOM'), 32, 0, C.cast(values, ptr), 2)
    select_input(d, w, (1 << 2) | (1 << 3))  # ButtonPress|ButtonRelease: the press sets the implicit grab
    mapwin(d, w)
    sync()
    return w


def set_motif(w):
    values = (ul * 5)(2, 0, 0, 0, 0)
    change(d, w, MOTIF, MOTIF, 32, 0, C.cast(values, ptr), 5)
    sync()


def client(title):
    return next((c for c in json.loads(ctl('clients', '-j')) if c.get('title') == title), None)


def wait_client(title, pred, label, timeout=2.0):
    end = time.time() + timeout
    c = None
    while time.time() < end:
        c = client(title)
        if c and pred(c):
            return c
        time.sleep(.02)
    raise AssertionError((label, c))


def hold(title, pred, label, seconds=0.6):
    """pred must stay true for the whole period (a negative check)."""
    end = time.time() + seconds
    while time.time() < end:
        c = client(title)
        assert c and pred(c), (label, c)
        time.sleep(.05)


def request(w, action, atoms=None):
    e = Event()
    e.xclient.type = 33
    e.xclient.display = d
    e.xclient.window = w
    e.xclient.message_type = STATE
    e.xclient.format = 32
    a1, a2 = atoms or (VERT, HORZ)  # both in one message, as Steam sends it
    e.xclient.data[:] = [action, a1, a2, 0, 0]
    send_event(d, root, 0, 0x180000, C.byref(e))
    sync()


def wait_click(w, title, n, what, timeout=60):
    store_name(d, w, title.encode())  # title unchanged: tests look windows up by title
    status(f'WAIT {n} {what}')
    end = time.time() + timeout
    e = Event()
    while time.time() < end:
        while pending(d):
            next_event(d, C.byref(e))
            if e.type == 5:  # ButtonRelease
                status(f'GOT {n}')
                return
        time.sleep(.01)
    raise AssertionError(f'no click for step {n}')


def wait_button(w, title, n, what, kind, timeout=60):
    """Wait for one X ButtonPress (4) or ButtonRelease (5); the runner clicks once."""
    store_name(d, w, title.encode())
    if kind == 4:
        status(f'WAIT {n} {what}')
    end = time.time() + timeout
    e = Event()
    while time.time() < end:
        while pending(d):
            next_event(d, C.byref(e))
            if e.type == kind:
                if kind == 5:
                    status(f'GOT {n}')
                return
        time.sleep(.005)
    raise AssertionError(f'no button event {kind} for step {n}')


def has_state_atoms(w):
    t, f, n, after = ul(), integer(), ul(), ul()
    data = ptr()
    get_prop(d, w, STATE, 0, 32, 0, 4, C.byref(t), C.byref(f), C.byref(n), C.byref(after), C.byref(data))
    atoms = [C.cast(data, C.POINTER(ul))[i] for i in range(n.value)] if data else []
    if data:
        xfree(data)
    return VERT in atoms and HORZ in atoms


def wait_reported(w, expected, label):
    for _ in range(100):
        if has_state_atoms(w) == expected:
            return
        time.sleep(.02)
    raise AssertionError((label, 'reported maximized', not expected))


maximized = lambda c: c['fullscreen'] == 1 and c['fullscreenClient'] == 1
normal = lambda c: c['fullscreen'] == 0 and c['fullscreenClient'] == 0
results = []
T = 'xcsd max fixture'
win = make_window(T)
try:
    base = wait_client(T, lambda c: 'x11-csd' in c['tags'] and c['floating'], 'tagged floating fixture')
    size = base['size']

    request(win, 1)
    hold(T, normal, 'maximize with no click stays suppressed')
    results.append('no click: suppressed')

    wait_click(win, T, 1, 'click to maximize (add)')
    request(win, 1)
    c = wait_client(T, maximized, 'click + add maximizes')
    assert c['size'][0] > size[0] and c['size'][1] > size[1], ('maximized size', c, size)
    wait_reported(win, True, 'the app is told it is maximized')
    hold(T, maximized, 'maximize holds (no bounce)')
    results.append('click + add: maximized (internal and client state, size), reported to the app, holds')

    # An app that reads the report sends remove from its restore button.
    wait_click(win, T, 2, 'click to restore (remove)')
    request(win, 0)
    c = wait_client(T, normal, 'click + remove restores')
    assert c['size'] == size, ('restored size', c, size)
    wait_reported(win, False, 'the app is told it is no longer maximized')
    results.append('click + remove: restored to previous size, report cleared')

    # An app that ignores the report sends add every time; add while maximized restores.
    wait_click(win, T, 3, 'click to maximize (add)')
    request(win, 1)
    wait_client(T, maximized, 'click + add maximizes again')
    wait_click(win, T, 4, 'click to restore (add again, report ignored)')
    request(win, 1)
    wait_client(T, normal, 'click + repeated add restores')
    results.append('click + repeated add (report ignored): restores')

    wait_click(win, T, 5, 'click + remove while unmaximized')
    request(win, 0)
    hold(T, normal, 'remove never maximizes')
    results.append('click + remove while unmaximized: unchanged')

    # One click, one change: VERT and HORZ in two messages must not cancel out.
    wait_click(win, T, 51, 'click, then VERT and HORZ in separate messages')
    request(win, 2, (VERT, 0))
    request(win, 2, (HORZ, 0))
    wait_client(T, maximized, 'first message maximizes')
    hold(T, maximized, 'second message in the same second needs a new click')
    results.append('separate VERT/HORZ messages: one change per click')

    # An app acting on the press: its release must not re-arm the consumed click.
    wait_click(win, T, 54, 'click + remove to restore')
    request(win, 0)
    wait_client(T, normal, 'restore before press case')
    wait_button(win, T, 55, 'click (maximize on press)', 4)
    request(win, 1)
    wait_client(T, maximized, 'add on press maximizes')
    wait_button(win, T, 55, '', 5)
    request(win, 1)
    hold(T, maximized, 'a second add after the release needs a new click')
    results.append('request on press: its release does not re-arm the click')

    # Maximized by the compositor (Omarchy's keybind, not the app): report follows,
    # and the app's restore request works.
    wait_click(win, T, 52, 'click + remove to restore')
    request(win, 0)
    wait_client(T, normal, 'restore before keybind case')
    addr = client(T)['address']
    out = ctl('dispatch', 'hl.dsp.window.fullscreen({ mode = "maximized", action = "set", window = "address:%s" })' % addr).strip()
    wait_client(T, maximized, 'compositor maximize (%s)' % out)
    wait_reported(win, True, 'compositor maximize is reported')
    wait_click(win, T, 53, 'click + remove after a compositor maximize')
    request(win, 0)
    wait_client(T, normal, 'app restores a compositor maximize')
    wait_reported(win, False, 'restore is reported')
    results.append('compositor maximize: reported; app restore works')

    wait_click(win, T, 6, 'click, then a late request')
    time.sleep(1.4)
    request(win, 1)
    hold(T, normal, 'request well after a click stays suppressed')
    results.append('late request: suppressed')

    delete(d, win, MOTIF)
    sync()
    wait_client(T, lambda c: 'x11-csd' not in c['tags'], 'hint removed untags')
    wait_click(win, T, 7, 'click on untagged window')
    request(win, 1)
    hold(T, normal, 'untagged window keeps the suppress rule')
    results.append('untagged: suppressed')

    P = 'xcsd max premapped'
    pre = make_window(P, premaximized=True)
    try:
        wait_client(P, lambda c: 'x11-csd' in c['tags'], 'premapped fixture tagged')
        hold(P, normal, 'maximized state set before map stays suppressed')
        results.append('pre-map maximized state: suppressed')
    finally:
        destroy(d, pre)
        sync()

    print(json.dumps({'passed': results}, indent=2), flush=True)
    status('DONE')
except BaseException as ex:
    status(f'FAIL {ex!r}')
    raise
finally:
    destroy(d, win)
    sync()
    api('XCloseDisplay', integer, [ptr])(d)
