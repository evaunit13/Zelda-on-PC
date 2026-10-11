"""Minimal RPX/RPL loader + PPC disassembly helpers for reverse engineering U-King.rpx.

Usage:
  python -I rpx.py dis <addr> [count]        disassemble count instructions from addr
  python -I rpx.py func <addr>               disassemble from the nearest preceding 'stwu r1' to the next blr
  python -I rpx.py xref <addr>               find lis/addi|lwz|lfs pairs referencing addr (data refs) and bl to addr
  python -I rpx.py find-float <value>        find a float in data sections
  python -I rpx.py str <substring>           find ascii strings in data sections
  python -I rpx.py sections

The executable is read from your own game dump: set UKING_RPX to the path of code/U-King.rpx of the 1.5.0 update.
"""
import struct, sys, zlib, os
import capstone

DEFAULT_RPX = os.environ.get('UKING_RPX', '')

SHF_RPL_ZLIB = 0x08000000


class Rpx:
    def __init__(self, path=DEFAULT_RPX):
        cache = os.path.join(os.path.dirname(os.path.abspath(__file__)), '.rpxcache')
        os.makedirs(cache, exist_ok=True)
        if not path:
            raise SystemExit('set UKING_RPX to the path of code/U-King.rpx (update 1.5.0) of your game dump')
        raw = open(path, 'rb').read()
        assert raw[:4] == b'\x7fELF'
        (e_shoff,) = struct.unpack_from('>I', raw, 0x20)
        e_shentsize, e_shnum, e_shstrndx = struct.unpack_from('>HHH', raw, 0x2E)
        self.sections = []
        for i in range(e_shnum):
            off = e_shoff + i * e_shentsize
            name, typ, flags, addr, offset, size, link, info, align, entsize = struct.unpack_from('>10I', raw, off)
            data = b''
            if typ != 8 and size:  # not NOBITS
                data = raw[offset:offset + size]
                if flags & SHF_RPL_ZLIB:
                    data = zlib.decompress(data[4:])
            self.sections.append(dict(idx=i, name_off=name, type=typ, flags=flags, addr=addr, size=len(data) if data else size, data=data, link=link, info=info))
        shstr = self.sections[e_shstrndx]['data']
        for s in self.sections:
            n = s['name_off']
            s['name'] = shstr[n:shstr.index(b'\0', n)].decode() if shstr else ''
        self.text = [s for s in self.sections if s['addr'] and s['data'] and (s['flags'] & 4)]
        self.datas = [s for s in self.sections if s['addr'] and s['data'] and not (s['flags'] & 4)]
        self.md = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_32 | capstone.CS_MODE_BIG_ENDIAN | capstone.CS_MODE_PS)
        self.md.detail = False
        self._symbols = None

    def sec_for(self, addr):
        for s in self.sections:
            if s['addr'] and s['data'] and s['addr'] <= addr < s['addr'] + len(s['data']):
                return s
        return None

    def read(self, addr, n):
        s = self.sec_for(addr)
        if not s:
            return None
        o = addr - s['addr']
        return s['data'][o:o + n]

    def u32(self, addr):
        return struct.unpack('>I', self.read(addr, 4))[0]

    def f32(self, addr):
        return struct.unpack('>f', self.read(addr, 4))[0]

    def symbols(self):
        if self._symbols is None:
            self._symbols = {}
            for s in self.sections:
                if s['type'] == 2 and s['data']:  # SYMTAB
                    strtab = self.sections[s['link']]['data']
                    d = s['data']
                    for i in range(0, len(d), 16):
                        name, value, size, info, other, shndx = struct.unpack_from('>IIIBBH', d, i)
                        if name and value:
                            nm = strtab[name:strtab.index(b'\0', name)].decode('latin1')
                            self._symbols[value] = nm
        return self._symbols

    def dis(self, addr, count):
        code = self.read(addr, count * 4)
        out = []
        syms = self.symbols()
        for i in range(0, len(code), 4):
            a = addr + i
            w = code[i:i + 4]
            ins = list(self.md.disasm(w, a))
            txt = f"{ins[0].mnemonic:8} {ins[0].op_str}" if ins else f".word 0x{struct.unpack('>I', w)[0]:08X}"
            lab = f"  <{syms[a]}>" if a in syms else ''
            out.append(f"{a:08X}: {struct.unpack('>I', w)[0]:08X}  {txt}{lab}")
        return out

    def func_bounds(self, addr):
        # walk backwards to 'stwu r1,-X(r1)' (0x9421xxxx)
        a = addr & ~3
        start = a
        for _ in range(4000):
            w = self.u32(a)
            if (w & 0xFFFF0000) == 0x94210000:
                start = a
                break
            a -= 4
        a = start
        end = addr
        for _ in range(8000):
            w = self.u32(a)
            if w == 0x4E800020 and a >= addr:  # blr
                end = a
                break
            a += 4
        return start, end

    def iter_text(self):
        for s in self.text:
            d = s['data']
            base = s['addr']
            for i in range(0, len(d) - 3, 4):
                yield base + i, struct.unpack_from('>I', d, i)[0]

    def xrefs(self, target):
        """Find code referencing target: lis rX,hi ; (addi|ori|lwz|lfs|stw|stfs|lhz...) rY, lo(rX) within a small window; and branches."""
        hi_ha = ((target + 0x8000) >> 16) & 0xFFFF
        hi = (target >> 16) & 0xFFFF
        lo = target & 0xFFFF
        res = []
        words = list(self.iter_text())
        for idx, (a, w) in enumerate(words):
            op = w >> 26
            # branches
            if op == 18:
                li = w & 0x03FFFFFC
                if li & 0x02000000:
                    li -= 0x04000000
                dest = li if (w & 2) else a + li
                if dest == target:
                    res.append((a, 'bl' if w & 1 else 'b'))
            if op == 15 and ((w >> 16) & 0x1F) == 0:  # lis rD
                rd = (w >> 21) & 0x1F
                imm = w & 0xFFFF
                if imm not in (hi_ha, hi):
                    continue
                for j in range(1, 12):
                    if idx + j >= len(words):
                        break
                    a2, w2 = words[idx + j]
                    op2 = w2 >> 26
                    ra = (w2 >> 16) & 0x1F
                    d = w2 & 0xFFFF
                    sd = d - 0x10000 if d & 0x8000 else d
                    if ra != rd:
                        continue
                    if op2 == 24 and imm == hi and d == lo:  # ori
                        res.append((a2, 'ori'))
                    elif op2 in (14, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 60, 61) and imm == hi_ha and ((imm << 16) + sd) & 0xFFFFFFFF == target:
                        res.append((a2, f'op{op2}'))
        return res

    def find_bytes(self, pat, sections=None):
        res = []
        for s in (sections or self.datas):
            d = s['data']
            i = d.find(pat)
            while i >= 0:
                res.append(s['addr'] + i)
                i = d.find(pat, i + 1)
        return res


def main():
    r = Rpx()
    cmd = sys.argv[1]
    if cmd == 'sections':
        for s in r.sections:
            print(f"{s['idx']:3} {s['name']:24} type={s['type']:#x} flags={s['flags']:#010x} addr={s['addr']:#010x} size={s['size']:#x}")
    elif cmd == 'dis':
        a = int(sys.argv[2], 16)
        n = int(sys.argv[3]) if len(sys.argv) > 3 else 40
        print('\n'.join(r.dis(a, n)))
    elif cmd == 'func':
        a = int(sys.argv[2], 16)
        s, e = r.func_bounds(a)
        print(f"; function {s:08X}..{e:08X}")
        print('\n'.join(r.dis(s, (e - s) // 4 + 1)))
    elif cmd == 'xref':
        a = int(sys.argv[2], 16)
        for x, k in r.xrefs(a):
            fs, fe = r.func_bounds(x)
            print(f"{x:08X} {k:6} in func {fs:08X}")
    elif cmd == 'find-float':
        v = float(sys.argv[2])
        for a in r.find_bytes(struct.pack('>f', v)):
            print(f"{a:08X}")
    elif cmd == 'str':
        sub = sys.argv[2].encode()
        for a in r.find_bytes(sub):
            s = r.sec_for(a)
            o = a - s['addr']
            st = s['data'].rfind(b'\0', 0, o) + 1
            en = s['data'].find(b'\0', o)
            print(f"{s['addr'] + st:08X}: {s['data'][st:en].decode('latin1')}")


if __name__ == '__main__':
    main()
