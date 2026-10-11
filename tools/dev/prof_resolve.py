"""Resolve a profiler report of the developer bridge (prof stop <path>) against the linker map.
Usage: python -I prof_resolve.py <report.txt> [map file] [top]
Prints, per sampled thread, native samples aggregated by function (demangled with undname when available)."""
import bisect
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
report = sys.argv[1]
mapfile = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, 'bin', 'ZeldaOnPC_release.map')
top = int(sys.argv[3]) if len(sys.argv) > 3 else 25
UNDNAME = shutil.which('undname') or ''  # on the PATH of a Visual Studio developer prompt

base = 0x140000000
syms = []
with open(mapfile, encoding='latin-1') as f:
    for line in f:
        m = re.match(r'\s*Preferred load address is ([0-9a-fA-F]+)', line)
        if m:
            base = int(m.group(1), 16)
            continue
        m = re.match(r'\s*0001:[0-9a-fA-F]+\s+(\S+)\s+([0-9a-fA-F]{16})\s', line)
        if m:
            syms.append((int(m.group(2), 16) - base, m.group(1)))
syms.sort()
addrs = [s[0] for s in syms]


def lookup(rva):
    i = bisect.bisect_right(addrs, rva) - 1
    return syms[i][1] if i >= 0 else '?'


demangled = {}


def demangle(name):
    if name in demangled:
        return demangled[name]
    out = name
    if name.startswith('?') and UNDNAME:
        r = subprocess.run([UNDNAME, '0x2000', name], capture_output=True, text=True).stdout  # 0x2000: name only
        m = re.search(r'is :- "(.*)"', r)
        if m:
            out = m.group(1)
    demangled[name] = out
    return out


thread = None
agg = {}
with open(report, encoding='utf-8') as f:
    for line in f:
        m = re.match(r'== (.*?) : (\d+) samples(.*)', line)
        if m:
            thread = m.group(1)
            agg[thread] = (m.group(2) + m.group(3), {})
            continue
        m = re.match(r'\s*([\d.]+)%\s+native (\S+)\+([0-9a-f]+)', line)
        if m and thread:
            pct, mod, rva = float(m.group(1)), m.group(2), int(m.group(3), 16)
            key = lookup(rva) if mod.lower().startswith('breathofthewild') else mod
            d = agg[thread][1]
            d[key] = d.get(key, 0) + pct

for t, (hdr, d) in agg.items():
    print(f'\n== {t} : {hdr.strip()}')
    for k, v in sorted(d.items(), key=lambda kv: -kv[1])[:top]:
        print(f'{v:6.2f}%  {demangle(k)[:150]}')
