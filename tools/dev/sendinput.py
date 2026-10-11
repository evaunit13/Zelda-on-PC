"""Synthesize real keyboard/mouse input with SendInput (goes through Raw Input like physical devices).
Usage examples:
  python -I sendinput.py focus "Breath of the Wild"
  python -I sendinput.py tap VK_ESCAPE | down 0x57 | up 0x57 | move 100 0 | wheel -1 | click left
"""
import ctypes
import ctypes.wintypes as wt
import sys
import time

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32

INPUT_MOUSE, INPUT_KEYBOARD = 0, 1
KEYEVENTF_EXTENDEDKEY, KEYEVENTF_KEYUP, KEYEVENTF_SCANCODE = 0x1, 0x2, 0x8
MOUSEEVENTF_MOVE, MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP = 0x1, 0x2, 0x4
MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP, MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP = 0x8, 0x10, 0x20, 0x40
MOUSEEVENTF_WHEEL = 0x800

ULONG_PTR = ctypes.c_size_t


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [('dx', wt.LONG), ('dy', wt.LONG), ('mouseData', wt.DWORD), ('dwFlags', wt.DWORD), ('time', wt.DWORD), ('dwExtraInfo', ULONG_PTR)]


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [('wVk', wt.WORD), ('wScan', wt.WORD), ('dwFlags', wt.DWORD), ('time', wt.DWORD), ('dwExtraInfo', ULONG_PTR)]


class HARDWAREINPUT(ctypes.Structure):
    _fields_ = [('uMsg', wt.DWORD), ('wParamL', wt.WORD), ('wParamH', wt.WORD)]


class _U(ctypes.Union):
    _fields_ = [('mi', MOUSEINPUT), ('ki', KEYBDINPUT), ('hi', HARDWAREINPUT)]


class INPUT(ctypes.Structure):
    _fields_ = [('type', wt.DWORD), ('u', _U)]


EXTENDED = {0x25, 0x26, 0x27, 0x28, 0x2D, 0x2E, 0x21, 0x22, 0x23, 0x24, 0xA3, 0xA5, 0x6F, 0x90}


def _send(*inputs):
    arr = (INPUT * len(inputs))(*inputs)
    user32.SendInput(len(inputs), arr, ctypes.sizeof(INPUT))


def key(vk, down):
    sc = user32.MapVirtualKeyW(vk, 0)
    flags = KEYEVENTF_SCANCODE | (0 if down else KEYEVENTF_KEYUP)
    if vk in EXTENDED:
        flags |= KEYEVENTF_EXTENDEDKEY
    i = INPUT(type=INPUT_KEYBOARD)
    i.u.ki = KEYBDINPUT(vk, sc, flags, 0, 0)
    _send(i)


def tap(vk, hold=0.06):
    key(vk, True)
    time.sleep(hold)
    key(vk, False)


def move(dx, dy):
    i = INPUT(type=INPUT_MOUSE)
    i.u.mi = MOUSEINPUT(int(dx), int(dy), 0, MOUSEEVENTF_MOVE, 0, 0)
    _send(i)


def wheel(notches):
    i = INPUT(type=INPUT_MOUSE)
    i.u.mi = MOUSEINPUT(0, 0, ctypes.c_uint32(int(notches * 120)).value, MOUSEEVENTF_WHEEL, 0, 0)
    _send(i)


def click(button='left', hold=0.06):
    d, u = {'left': (MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP), 'right': (MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP),
            'middle': (MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP)}[button]
    i = INPUT(type=INPUT_MOUSE)
    i.u.mi = MOUSEINPUT(0, 0, 0, d, 0, 0)
    _send(i)
    time.sleep(hold)
    i.u.mi = MOUSEINPUT(0, 0, 0, u, 0, 0)
    _send(i)


def find_window(sub):
    found = []
    EnumProc = ctypes.WINFUNCTYPE(ctypes.c_bool, wt.HWND, wt.LPARAM)

    def cb(hwnd, lp):
        if user32.IsWindowVisible(hwnd):
            n = user32.GetWindowTextLengthW(hwnd)
            buf = ctypes.create_unicode_buffer(n + 1)
            user32.GetWindowTextW(hwnd, buf, n + 1)
            if sub.lower() in buf.value.lower():
                found.append(hwnd)
        return True
    user32.EnumWindows(EnumProc(cb), 0)
    return found[0] if found else None


def focus(sub):
    hwnd = find_window(sub)
    if not hwnd:
        return False
    # Alt tap makes the foreground lock allow SetForegroundWindow from this process
    key(0x12, True)
    key(0x12, False)
    user32.ShowWindow(hwnd, 9)
    user32.SetForegroundWindow(hwnd)
    time.sleep(0.3)
    return user32.GetForegroundWindow() == hwnd


VK = {'VK_ESCAPE': 0x1B, 'VK_RETURN': 0x0D, 'VK_TAB': 0x09, 'VK_SPACE': 0x20, 'VK_UP': 0x26, 'VK_DOWN': 0x28,
      'VK_LEFT': 0x25, 'VK_RIGHT': 0x27, 'VK_LSHIFT': 0xA0, 'VK_BACK': 0x08, 'VK_F1': 0x70, 'VK_DELETE': 0x2E}

if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'focus':
        print(focus(sys.argv[2]))
    elif cmd in ('tap', 'down', 'up'):
        v = sys.argv[2]
        vk = VK.get(v) or (ord(v.upper()) if len(v) == 1 else int(v, 0))
        {'tap': lambda: tap(vk), 'down': lambda: key(vk, True), 'up': lambda: key(vk, False)}[cmd]()
    elif cmd == 'move':
        move(int(sys.argv[2]), int(sys.argv[3]))
    elif cmd == 'wheel':
        wheel(int(sys.argv[2]))
    elif cmd == 'click':
        click(sys.argv[2] if len(sys.argv) > 2 else 'left')
