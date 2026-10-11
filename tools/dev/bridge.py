"""Client for the developer bridge (127.0.0.1:28770, only when the game runs with --pce-dev)."""
import socket
import struct
import sys
import time

VPAD = dict(A=0x8000, B=0x4000, X=0x2000, Y=0x1000, L=0x20, R=0x10, ZL=0x80, ZR=0x40, PLUS=0x8, MINUS=0x4,
            UP=0x200, DOWN=0x100, LEFT=0x800, RIGHT=0x400, LSTICK=0x40000, RSTICK=0x20000)


class Bridge:
    def __init__(self, host='127.0.0.1', port=28770):
        self.s = socket.create_connection((host, port), timeout=10)
        self.f = self.s.makefile('rw', newline='\n', encoding='utf-8', errors='replace')

    def cmd(self, line):
        self.f.write(line + '\n')
        self.f.flush()
        return self.f.readline().strip()

    def frame(self):
        return int(self.cmd('frame'))

    def wait_frames(self, n, timeout=30):
        start = self.frame()
        t0 = time.time()
        while self.frame() - start < n:
            if time.time() - t0 > timeout:
                raise TimeoutError('frames not advancing')
            time.sleep(0.004)

    def press(self, name, frames=4, release_frames=4):
        mask = 0
        for n in name.split('+'):
            mask |= VPAD[n]
        self.cmd(f'buttons {mask}')
        self.wait_frames(frames)
        self.cmd('buttons 0')
        self.wait_frames(release_frames)

    def hold(self, name):
        mask = 0
        for n in name.split('+'):
            if n:
                mask |= VPAD[n]
        self.cmd(f'buttons {mask}')

    def readf(self, addr, n=1):
        r = self.cmd(f'readf {addr:#x} {n}')
        if r.startswith('err'):
            raise RuntimeError(r)
        return [float(x) for x in r.split()]

    def read32(self, addr, n=1):
        r = self.cmd(f'read32 {addr:#x} {n}')
        if r.startswith('err'):
            raise RuntimeError(r)
        return [int(x, 16) for x in r.split()]

    def readbytes(self, addr, n):
        r = self.cmd(f'readhex {addr:#x} {n}')
        if r.startswith('err'):
            raise RuntimeError(r)
        return bytes.fromhex(r)

    def screenshot(self, path):
        return self.cmd(f'screenshot {path}')


if __name__ == '__main__':
    b = Bridge()
    for line in sys.argv[1:]:
        print(b.cmd(line))
