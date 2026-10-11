"""Capture a top-level window (by title substring or pid) to PNG using screen grab of its rect.
Usage: python -I wincap.py <title-substring|pid:N> <out.png>"""
import ctypes, ctypes.wintypes as wt, sys
from PIL import ImageGrab
user32 = ctypes.windll.user32
user32.SetProcessDPIAware()
target = sys.argv[1]
found = []
EnumProc = ctypes.WINFUNCTYPE(ctypes.c_bool, wt.HWND, wt.LPARAM)
def cb(hwnd, lp):
    if not user32.IsWindowVisible(hwnd):
        return True
    n = user32.GetWindowTextLengthW(hwnd)
    buf = ctypes.create_unicode_buffer(n + 1)
    user32.GetWindowTextW(hwnd, buf, n + 1)
    pid = wt.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    if target.startswith('pid:'):
        if pid.value == int(target[4:]) and n > 0:
            found.append((hwnd, buf.value))
    elif target.lower() in buf.value.lower():
        found.append((hwnd, buf.value))
    return True
user32.EnumWindows(EnumProc(cb), 0)
if not found:
    print('window not found'); sys.exit(1)
hwnd, title = found[0]
r = wt.RECT()
user32.GetWindowRect(hwnd, ctypes.byref(r))
img = ImageGrab.grab(bbox=(r.left, r.top, r.right, r.bottom), all_screens=True)
img.save(sys.argv[2])
print(title, r.left, r.top, r.right, r.bottom)
