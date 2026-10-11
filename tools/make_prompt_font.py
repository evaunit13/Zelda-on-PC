"""Builds the keyboard / mouse button prompt font: a BFFNT (Cafe v3) that replaces BotW's External_00.bffnt.

All glyph artwork is drawn here (keycaps with cut-out labels, mouse buttons, composites) in the style of the
original GamePad glyphs (white shape, label cut out). No game data is used.

Code points:
  E040..E08F  the game's own button codes; this table is rewritten at startup from the player's bindings
  E0F0..E0FF  composites (WASD, A/D, mouse movement, wheel, arrow keys, 1-4)
  E100+VK     one glyph per key / mouse button (VK = Windows virtual-key code, pce::Key)

Usage: python -I make_prompt_font.py <out.bffnt> [preview.png]
"""
import os, struct, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gx2tex

SS = 4                      # supersampling
CELL_W, CELL_H = 72, 39     # glyph cell (original: 42x39)
BASELINE = 31
SHEET_W, SHEET_H = 512, 1024
COLS = SHEET_W // (CELL_W + 1)
ROWS = SHEET_H // (CELL_H + 1)
CAP_TOP, CAP_BOTTOM = 7, 35  # keycap vertical extent (same as the original A/B/X/Y circles: 7..34)

# label typeface: Roboto Medium from the Dear ImGui submodule (Apache License 2.0)
FONT_BOLD = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'dependencies', 'imgui', 'misc', 'fonts', 'Roboto-Medium.ttf')

# ---------------------------------------------------------------- keys
NAMED = {
    0x08: 'Bksp', 0x09: 'Tab', 0x0D: 'Enter', 0x14: 'Caps', 0x1B: 'Esc', 0x20: 'Space',
    0x21: 'PgUp', 0x22: 'PgDn', 0x23: 'End', 0x24: 'Home', 0x2D: 'Ins', 0x2E: 'Del',
    0x60: 'Num0', 0x61: 'Num1', 0x62: 'Num2', 0x63: 'Num3', 0x64: 'Num4', 0x65: 'Num5', 0x66: 'Num6',
    0x67: 'Num7', 0x68: 'Num8', 0x69: 'Num9', 0x6A: 'Num*', 0x6B: 'Num+', 0x6D: 'Num-', 0x6E: 'Num.', 0x6F: 'Num/',
    0xA0: 'Shift', 0xA1: 'RShift', 0xA2: 'Ctrl', 0xA3: 'RCtrl', 0xA4: 'Alt', 0xA5: 'AltGr',
    0xBA: ';', 0xBB: '=', 0xBC: ',', 0xBD: '-', 0xBE: '.', 0xBF: '/', 0xC0: '`',
    0xDB: '[', 0xDC: '\\', 0xDD: ']', 0xDE: "'",
}
for i in range(12):
    NAMED[0x70 + i] = f'F{i + 1}'
for c in range(0x30, 0x3A):
    NAMED[c] = chr(c)
for c in range(0x41, 0x5B):
    NAMED[c] = chr(c)
ARROW_KEYS = {0x25: 'left', 0x26: 'up', 0x27: 'right', 0x28: 'down'}
MOUSE = {0x01: 'L', 0x02: 'R', 0x04: 'M', 0x05: 'X1', 0x06: 'X2', 0x0E: 'WU', 0x0F: 'WD'}


# ---------------------------------------------------------------- drawing helpers (work at SS scale)
def canvas():
    return Image.new('L', (CELL_W * SS, CELL_H * SS), 0)


def font(px):
    return ImageFont.truetype(FONT_BOLD, int(px * SS))


def text_size(fnt, s):
    l, t, r, b = fnt.getbbox(s)
    return r - l, b - t, l, t


def rrect(d, x0, y0, x1, y1, r, fill):
    d.rounded_rectangle([x0 * SS, y0 * SS, x1 * SS - 1, y1 * SS - 1], radius=r * SS, fill=fill)


def cutout_text(d, s, cx, cy, px):
    fnt = font(px)
    w, h, l, t = text_size(fnt, s)
    # a thin outline in the label colour brings the medium weight close to a bold face
    d.text((cx * SS - w / 2 - l, cy * SS - h / 2 - t), s, font=fnt, fill=0, stroke_width=2 if px >= 15 else 1, stroke_fill=0)


def label_px(s):
    if len(s) == 1:
        return 17
    if len(s) <= 3:
        return 12.5
    return 11


def keycap_width(s):
    px = label_px(s)
    w, _, _, _ = text_size(font(px), s)
    return max(28, int(round(w / SS + 12)))


def draw_keycap(img, s, x0=1, y0=CAP_TOP, h=CAP_BOTTOM - CAP_TOP, w=None, px=None, arrow=None):
    d = ImageDraw.Draw(img)
    w = w or keycap_width(s)
    rrect(d, x0, y0, x0 + w, y0 + h, 6 if h > 20 else 3.5, 255)
    cx, cy = x0 + w / 2, y0 + h / 2
    if arrow:
        triangle(d, cx, cy, h * 0.42, arrow, 0)
    else:
        cutout_text(d, s, cx, cy + 0.3, px or label_px(s))
    return x0 + w


def triangle(d, cx, cy, size, direction, fill):
    s = size / 2
    pts = {
        'up': [(cx, cy - s), (cx + s, cy + s * 0.8), (cx - s, cy + s * 0.8)],
        'down': [(cx, cy + s), (cx + s, cy - s * 0.8), (cx - s, cy - s * 0.8)],
        'left': [(cx - s, cy), (cx + s * 0.8, cy - s), (cx + s * 0.8, cy + s)],
        'right': [(cx + s, cy), (cx - s * 0.8, cy - s), (cx - s * 0.8, cy + s)],
    }[direction]
    d.polygon([(x * SS, y * SS) for x, y in pts], fill=fill)


def draw_mouse(img, x0, part=None, arrows=(), wheel_arrow=None):
    """Mouse seen from above: outline body, highlighted part ('L', 'R', 'M', 'X1', 'X2', 'body')."""
    d = ImageDraw.Draw(img)
    w, h = 19, 27
    y0 = CAP_TOP + 0.5
    x1, y1 = x0 + w, y0 + h
    stroke = 2.4
    # body
    d.rounded_rectangle([x0 * SS, y0 * SS, x1 * SS, y1 * SS], radius=9 * SS, fill=255)
    d.rounded_rectangle([(x0 + stroke) * SS, (y0 + stroke) * SS, (x1 - stroke) * SS, (y1 - stroke) * SS],
                        radius=(9 - stroke) * SS, fill=0)
    split = y0 + 11
    mid = (x0 + x1) / 2
    # button separation
    d.rectangle([x0 * SS, (split - 1.1) * SS, x1 * SS, (split + 1.1) * SS], fill=255)
    d.rectangle([(mid - 1.1) * SS, y0 * SS, (mid + 1.1) * SS, split * SS], fill=255)
    if part == 'L':
        d.rounded_rectangle([x0 * SS, y0 * SS, mid * SS, split * SS], radius=9 * SS, fill=255, corners=(True, False, False, False))
        d.rectangle([(x0 + 4) * SS, (y0 + 4) * SS, mid * SS, split * SS], fill=255)
    elif part == 'R':
        d.rounded_rectangle([mid * SS, y0 * SS, x1 * SS, split * SS], radius=9 * SS, fill=255, corners=(False, True, False, False))
        d.rectangle([mid * SS, (y0 + 4) * SS, (x1 - 4) * SS, split * SS], fill=255)
    if part in ('M', 'WU', 'WD'):
        # wheel: filled pill across the split
        d.rounded_rectangle([(mid - 2.6) * SS, (y0 + 3.5) * SS, (mid + 2.6) * SS, (split + 3.5) * SS], radius=2.6 * SS, fill=255)
        d.rounded_rectangle([(mid - 1.0) * SS, (y0 + 5.2) * SS, (mid + 1.0) * SS, (split + 1.8) * SS], radius=1 * SS, fill=0)
    if part in ('X1', 'X2'):
        # side buttons on the left flank, the active one filled
        for i, name in enumerate(('X2', 'X1')):
            yy = split + 2.5 + i * 6
            box = [(x0 - 0.2) * SS, yy * SS, (x0 + 4.5) * SS, (yy + 4.6) * SS]
            d.rounded_rectangle(box, radius=1.5 * SS, fill=255 if name == part else 0, outline=255, width=int(1.3 * SS))
    if wheel_arrow:
        ax = x1 + 6.5
        triangle(d, ax, CAP_TOP + (6 if wheel_arrow == 'up' else 22), 9, wheel_arrow, 255)
    for a in arrows:
        if a == 'left':
            triangle(d, x0 - 5, y0 + h / 2, 8, 'left', 255)
        elif a == 'right':
            triangle(d, x1 + 5, y0 + h / 2, 8, 'right', 255)
        elif a == 'up':
            triangle(d, mid, y0 - 4.5, 7, 'up', 255)
        elif a == 'down':
            triangle(d, mid, y1 + 4.5, 7, 'down', 255)
    return x1


def finish(img):
    """Downsample, return (cell image uint8, left, glyph width)."""
    small = img.resize((CELL_W, CELL_H), Image.LANCZOS)
    a = np.asarray(small, dtype=np.uint8).copy()
    cols = np.nonzero(a.max(axis=0) > 8)[0]
    if len(cols) == 0:
        return a, 0, 8
    x0, x1 = int(cols.min()), int(cols.max()) + 1
    # move the glyph to the cell's left edge (left bearing is stored in CWDH)
    out = np.zeros_like(a)
    out[:, :x1 - x0] = a[:, x0:x1]
    return out, 0, x1 - x0


# ---------------------------------------------------------------- glyph builders
def g_key(vk):
    img = canvas()
    if vk in ARROW_KEYS:
        draw_keycap(img, '', w=28, arrow=ARROW_KEYS[vk])
    else:
        draw_keycap(img, NAMED[vk])
    return img


def g_mouse(vk):
    img = canvas()
    kind = MOUSE[vk]
    if kind in ('WU', 'WD'):
        draw_mouse(img, 2, 'M', wheel_arrow='up' if kind == 'WU' else 'down')
    else:
        draw_mouse(img, 2 if kind not in ('X1', 'X2') else 3, kind)
    return img


def g_mouse_move(dirs):
    img = canvas()
    left = 'left' in dirs
    draw_mouse(img, 9 if left else 2, None, arrows=dirs)
    return img


def g_wheel_updown():
    img = canvas()
    d = ImageDraw.Draw(img)
    draw_mouse(img, 2, 'M')
    triangle(d, 27.5, CAP_TOP + 6, 8, 'up', 255)
    triangle(d, 27.5, CAP_TOP + 21, 8, 'down', 255)
    return img


def g_cluster(labels, arrows=False):
    """WASD-style cluster: top key centred over a row of three."""
    img = canvas()
    kw, kh, gap = 12.5, 12.5, 1.6
    top, row = labels[0], labels[1:]
    x0 = 1
    y_row = CAP_BOTTOM - kh
    y_top = y_row - gap - kh
    for i, lab in enumerate(row):
        xx = x0 + i * (kw + gap)
        draw_keycap(img, lab, x0=xx, y0=y_row, h=kh, w=kw, px=9.5,
                    arrow=(['left', 'down', 'right'][i] if arrows else None))
    draw_keycap(img, top, x0=x0 + kw + gap, y0=y_top, h=kh, w=kw, px=9.5, arrow=('up' if arrows else None))
    return img


def g_pair(a, b):
    img = canvas()
    w = 17
    x = draw_keycap(img, a, x0=1, w=w, px=13)
    draw_keycap(img, b, x0=x + 2.5, w=w, px=13)
    return img


def g_pair_vertical(a, b):
    img = canvas()
    kh = 12.5
    draw_keycap(img, a, x0=1, y0=CAP_TOP - 1, h=kh, w=15, px=9.5)
    draw_keycap(img, b, x0=1, y0=CAP_TOP - 1 + kh + 1.8, h=kh, w=15, px=9.5)
    return img


def g_arrow(kind):
    """Plain arrows / symbols for E083..E08F (tutorial texts)."""
    img = canvas()
    d = ImageDraw.Draw(img)
    cx, cy, L = 14, (CAP_TOP + CAP_BOTTOM) / 2, 22

    def arrow(angle_deg, double=False):
        import math
        a = math.radians(angle_deg)
        ux, uy = math.cos(a), -math.sin(a)
        px_, py_ = -uy, ux
        hl, hw, sw = 9, 8.5, 2.6
        tip = (cx + ux * L / 2, cy + uy * L / 2)
        tail = (cx - ux * L / 2, cy - uy * L / 2)
        base = (tip[0] - ux * hl, tip[1] - uy * hl)
        start = (tail[0] + ux * hl, tail[1] + uy * hl) if double else tail
        shaft = [(start[0] + px_ * sw, start[1] + py_ * sw), (base[0] + px_ * sw, base[1] + py_ * sw),
                 (base[0] - px_ * sw, base[1] - py_ * sw), (start[0] - px_ * sw, start[1] - py_ * sw)]
        d.polygon([(x * SS, y * SS) for x, y in shaft], fill=255)
        head = [tip, (base[0] + px_ * hw, base[1] + py_ * hw), (base[0] - px_ * hw, base[1] - py_ * hw)]
        d.polygon([(x * SS, y * SS) for x, y in head], fill=255)
        if double:
            base2 = (tail[0] + ux * hl, tail[1] + uy * hl)
            head2 = [tail, (base2[0] + px_ * hw, base2[1] + py_ * hw), (base2[0] - px_ * hw, base2[1] - py_ * hw)]
            d.polygon([(x * SS, y * SS) for x, y in head2], fill=255)

    if kind == 'lr':
        arrow(0, True)
    elif kind == 'ud':
        arrow(90, True)
    elif kind in ('cw', 'ccw'):
        # U-turn arrow
        bx = cx
        d.arc([(bx - 8) * SS, (cy - 9) * SS, (bx + 8) * SS, (cy + 7) * SS], 180, 360, fill=255, width=int(5 * SS))
        if kind == 'cw':
            d.rectangle([(bx - 8) * SS, (cy - 1) * SS, (bx - 3) * SS, (cy + 11) * SS], fill=255)
            triangle(d, bx + 5.5, cy + 6, 11, 'down', 255)
        else:
            d.rectangle([(bx + 3) * SS, (cy - 1) * SS, (bx + 8) * SS, (cy + 11) * SS], fill=255)
            triangle(d, bx - 5.5, cy + 6, 11, 'down', 255)
    elif kind == 'x':
        for sgn in (1, -1):
            pts = [(cx - 9, cy - 9 * sgn), (cx - 5.5, cy - 12.5 * sgn), (cx + 9, cy + 9 * sgn) if False else (cx + 12.5, cy + 5.5 * sgn)]
        w = 3.6
        for (x0, y0, x1, y1) in ((cx - 10, cy - 10, cx + 10, cy + 10), (cx - 10, cy + 10, cx + 10, cy - 10)):
            import math
            dx, dy = x1 - x0, y1 - y0
            n = math.hypot(dx, dy)
            px_, py_ = -dy / n * w, dx / n * w
            d.polygon([((x0 + px_) * SS, (y0 + py_) * SS), ((x1 + px_) * SS, (y1 + py_) * SS),
                       ((x1 - px_) * SS, (y1 - py_) * SS), ((x0 - px_) * SS, (y0 - py_) * SS)], fill=255)
    else:
        arrow({'r': 0, 'l': 180, 'u': 90, 'd': 270, 'ur': 45, 'ul': 135, 'dl': 225, 'ul2': 135}[kind])
    return img


def g_gamepad():
    img = canvas()
    d = ImageDraw.Draw(img)
    rrect(d, 1, CAP_TOP + 4, 33, CAP_BOTTOM - 4, 5, 255)
    rrect(d, 8, CAP_TOP + 8, 26, CAP_BOTTOM - 8, 1.5, 0)
    return img


def g_blank():
    return canvas()


# ---------------------------------------------------------------- glyph table
def build_glyphs():
    glyphs = []   # (code(s), image)
    for vk in sorted(MOUSE):
        glyphs.append(([0xE100 + vk], g_mouse(vk)))
    for vk in sorted(set(NAMED) | set(ARROW_KEYS)):
        glyphs.append(([0xE100 + vk], g_key(vk)))
    comps = {
        0xE0F0: g_cluster(['W', 'A', 'S', 'D']),
        0xE0F1: g_pair('A', 'D'),
        0xE0F2: g_pair_vertical('W', 'S'),
        0xE0F3: g_mouse_move(('left', 'right', 'up', 'down')),
        0xE0F4: g_mouse_move(('left', 'right')),
        0xE0F5: g_mouse_move(('up', 'down')),
        0xE0F6: g_cluster(['', '', '', ''], arrows=True),
        0xE0F7: g_wheel_updown(),
        0xE0F8: g_key_label('1-4'),
        0xE0F9: g_mouse_move(('left',)),
        0xE0FA: g_mouse_move(('right',)),
        0xE0FB: g_mouse_move(('up',)),
        0xE0FC: g_mouse_move(('down',)),
        0xE0FD: g_gamepad(),
        0xE0FE: g_blank(),
    }
    for code, img in comps.items():
        glyphs.append(([code], img))
    arrows = {0xE083: 'lr', 0xE084: 'ud', 0xE085: 'cw', 0xE086: 'ccw', 0xE087: 'r', 0xE088: 'l', 0xE089: 'u',
              0xE08A: 'd', 0xE08B: 'ur', 0xE08C: 'ul', 0xE08D: 'dl', 0xE08E: 'ul2', 0xE08F: 'x'}
    for code, kind in arrows.items():
        glyphs.append(([code], g_arrow(kind)))
    return glyphs


def g_key_label(s):
    img = canvas()
    draw_keycap(img, s)
    return img


# default meaning of the game's button codes (rewritten at startup from the bindings; VK or composite code)
DEFAULT_BASE = {
    0xE040: 0xE100 + 0x45,  # A  -> E (interact)
    0xE041: 0xE100 + 0xA0,  # B  -> Shift (sprint)
    0xE042: 0xE100 + 0x20,  # X  -> Space (jump)
    0xE043: 0xE100 + 0x01,  # Y  -> LMB (attack)
    0xE044: 0xE100 + 0x46,  # L  -> F (rune)
    0xE045: 0xE100 + 0x52,  # R  -> R (throw)
    0xE046: 0xE100 + 0x51,  # ZL -> Q (lock on / shield)
    0xE047: 0xE100 + 0x02,  # ZR -> RMB (bow)
    0xE048: 0xE100 + 0x1B,  # power -> Esc
    0xE049: 0xE0F8,         # D-pad -> 1-4
    0xE04A: 0xE100 + 0x1B,  # HOME -> Esc
    0xE04B: 0xE100 + 0x09,  # + -> Tab (inventory)
    0xE04C: 0xE100 + 0x4D,  # - -> M (map)
    0xE050: 0xE100 + 0x53,  # L stick down -> S
    0xE051: 0xE0FC,         # R stick down -> mouse down
    0xE052: 0xE100 + 0x57,  # L stick up -> W
    0xE053: 0xE0FB,         # R stick up -> mouse up
    0xE054: 0xE0F1,         # L stick left/right -> A D
    0xE055: 0xE0F4,         # R stick left/right -> mouse left/right
    0xE056: 0xE100 + 0x43,  # L stick press -> C (crouch)
    0xE057: 0xE100 + 0x04,  # R stick press -> MMB (scope)
    0xE058: 0xE100 + 0x44,  # L stick right -> D
    0xE059: 0xE0FA,         # R stick right -> mouse right
    0xE05A: 0xE100 + 0x41,  # L stick left -> A
    0xE05B: 0xE0F9,         # R stick left -> mouse left
    0xE05D: 0xE0F5,         # R stick up/down -> mouse up/down
    0xE05E: 0xE0F0,         # L stick -> WASD
    0xE05F: 0xE0F3,         # R stick -> mouse
    0xE060: 0xE100 + 0x34,  # D-pad up -> 4 (runes)
    0xE061: 0xE100 + 0x58,  # D-pad down -> X (whistle)
    0xE062: 0xE100 + 0x33,  # D-pad left -> 3 (shields)
    0xE063: 0xE100 + 0x31,  # D-pad right -> 1 (weapons)
    0xE064: 0xE0F7,         # D-pad up/down -> mouse wheel
    0xE065: 0xE0F6,         # D-pad left/right -> arrow keys
    0xE066: 0xE100 + 0x54,  # GamePad (tilt) -> T
    0xE067: 0xE0FE,         # blank
}


# ---------------------------------------------------------------- BFFNT writer
def build_font(glyphs):
    assert len(glyphs) <= COLS * ROWS, (len(glyphs), COLS * ROWS)
    sheet = np.zeros((SHEET_H, SHEET_W), dtype=np.uint8)
    widths = []
    code_to_index = {}
    for i, (codes, img) in enumerate(glyphs):
        cell, left, gw = finish(img)
        r, c = divmod(i, COLS)
        x, y = c * (CELL_W + 1), r * (CELL_H + 1)
        sheet[y:y + CELL_H, x:x + CELL_W] = cell
        widths.append((left, gw, gw + 2))
        for code in codes:
            code_to_index[code] = i
    for base, target in DEFAULT_BASE.items():
        code_to_index[base] = code_to_index[target]
    # texture: stored bottom-up, BC4, GX2 2D tiled (tile mode 4, swizzle 0)
    linear = gx2tex.bc4_encode(np.ascontiguousarray(sheet[::-1]))
    sheet_size = SHEET_W * SHEET_H // 2
    swz = gx2tex.swizzle_data(linear, SHEET_W // 4, SHEET_H // 4, 64, sheet_size, None, 4, 0)
    # roundtrip check
    back = gx2tex.bc4_decode(gx2tex.deswizzle(swz, SHEET_W // 4, SHEET_H // 4, 64, None, 4, 0), SHEET_W, SHEET_H)[::-1]
    assert np.abs(back.astype(int) - sheet.astype(int)).max() < 40

    E = '>'
    out = bytearray()
    out += b'FFNT' + struct.pack(E + 'HHIIHH', 0xFEFF, 0x14, 0x03000000, 0, 4, 0)
    finf_off = len(out)
    out += b'\0' * 0x20
    tglp_off = len(out)
    out += b'\0' * 0x20
    data_off = 0x2000
    out += b'\0' * (data_off - len(out))
    out += swz
    tglp_size = len(out) - tglp_off
    # CWDH
    cwdh_off = len(out)
    body = b''.join(struct.pack('bBb', *w) for w in widths)
    cwdh = struct.pack(E + 'HHI', 0, len(widths) - 1, 0) + body
    cwdh_size = 8 + len(cwdh)
    cwdh_size = (cwdh_size + 3) & ~3
    out += b'CWDH' + struct.pack(E + 'I', cwdh_size) + cwdh
    out += b'\0' * (cwdh_off + cwdh_size - len(out))
    # CMAPs (table method): E040..E08F, E0F0..E1FF
    ranges = [(0xE040, 0xE08F), (0xE0F0, 0xE1FF)]
    cmap_offs = []
    for lo, hi in ranges:
        off = len(out)
        cmap_offs.append(off)
        table = [code_to_index.get(c, 0xFFFF) for c in range(lo, hi + 1)]
        content = struct.pack(E + 'HHHHI', lo, hi, 1, 0, 0) + struct.pack(E + '%dH' % len(table), *table)
        size = (8 + len(content) + 3) & ~3
        out += b'CMAP' + struct.pack(E + 'I', size) + content
        out += b'\0' * (off + size - len(out))
    # link CMAPs
    for i in range(len(cmap_offs) - 1):
        struct.pack_into(E + 'I', out, cmap_offs[i] + 0x10, cmap_offs[i + 1] + 8)
    # FINF
    struct.pack_into(E + '4sIBBBBHHbBbBIII', out, finf_off, b'FINF', 0x20, 1, 50, 50, 36, 50, 0, 0, 42, 42, 1,
                     tglp_off + 8, cwdh_off + 8, cmap_offs[0] + 8)
    # TGLP
    struct.pack_into(E + '4sIBBBBIHHHHHHI', out, tglp_off, b'TGLP', tglp_size, CELL_W, CELL_H, 1, CELL_W,
                     sheet_size, BASELINE, 12, COLS, ROWS, SHEET_W, SHEET_H, data_off)
    struct.pack_into(E + 'I', out, 0x0C, len(out))
    return bytes(out), sheet, code_to_index


def main():
    out_path = sys.argv[1]
    glyphs = build_glyphs()
    data, sheet, cti = build_font(glyphs)
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    open(out_path, 'wb').write(data)
    print(f'{len(glyphs)} glyphs, {len(data)} bytes -> {out_path}')
    if len(sys.argv) > 2:
        # preview on grey, glyphs as white
        prev = Image.fromarray(sheet).convert('RGB')
        bg = Image.new('RGB', prev.size, (60, 70, 80))
        bg.paste((255, 255, 255), mask=Image.fromarray(sheet))
        used_rows = (len(glyphs) + COLS - 1) // COLS
        bg = bg.crop((0, 0, SHEET_W, used_rows * (CELL_H + 1)))
        bg = bg.resize((bg.width * 2, bg.height * 2), Image.NEAREST)
        bg.save(sys.argv[2])


if __name__ == '__main__':
    main()
