"""GX2 surface (de)swizzling (R600 addrlib port, Wii U params: 2 pipes, 4 banks, 256B interleave)
plus BC4 decode/encode helpers. Pure Python + numpy.

Swizzle mapping is a bijection, so the same address function is used for both directions.
"""
import numpy as np

NUM_PIPES = 2
NUM_BANKS = 4
GROUP_BITS = 8
PIPE_BITS = 1
BANK_BITS = 2


def surface_thickness(tile_mode):
    return 4 if tile_mode in (3, 7, 11, 13, 15) else (8 if tile_mode in (16, 17) else 1)


def pixel_index_micro(x, y, bpp):
    if bpp == 8:
        b = [x & 1, (x >> 1) & 1, (x >> 2) & 1, (y >> 1) & 1, y & 1, (y >> 2) & 1]
    elif bpp == 16:
        b = [x & 1, (x >> 1) & 1, (x >> 2) & 1, y & 1, (y >> 1) & 1, (y >> 2) & 1]
    elif bpp in (32, 96):
        b = [x & 1, (x >> 1) & 1, y & 1, (x >> 2) & 1, (y >> 1) & 1, (y >> 2) & 1]
    elif bpp == 64:
        b = [x & 1, y & 1, (x >> 1) & 1, (x >> 2) & 1, (y >> 1) & 1, (y >> 2) & 1]
    elif bpp == 128:
        b = [y & 1, x & 1, (x >> 1) & 1, (x >> 2) & 1, (y >> 1) & 1, (y >> 2) & 1]
    else:
        b = [x & 1, (x >> 1) & 1, y & 1, (x >> 2) & 1, (y >> 1) & 1, (y >> 2) & 1]
    return b[0] | (b[1] << 1) | (b[2] << 2) | (b[3] << 3) | (b[4] << 4) | (b[5] << 5)


def addr_micro_tiled(x, y, bpp, pitch, height, tile_mode):
    thickness = surface_thickness(tile_mode)
    micro_tile_bytes = (64 * thickness * bpp + 7) // 8
    micro_tiles_per_row = pitch >> 3
    micro_x = x >> 3
    micro_y = y >> 3
    micro_offset = micro_tile_bytes * (micro_x + micro_y * micro_tiles_per_row)
    pixel_index = pixel_index_micro(x, y, bpp)
    return micro_offset + ((bpp * pixel_index) >> 3)


def addr_macro_tiled(x, y, bpp, pitch, height, tile_mode, pipe_swizzle, bank_swizzle):
    thickness = surface_thickness(tile_mode)
    pixel_index = pixel_index_micro(x, y, bpp)
    elem_offset = (bpp * pixel_index) >> 3
    pipe = ((y >> 3) ^ (x >> 3)) & 1
    bank_bit0 = ((y // (16 * NUM_PIPES)) ^ (x >> 3)) & 1
    bank_bit1 = ((y // (8 * NUM_PIPES)) ^ (x >> 4)) & 1
    bank = bank_bit0 | (bank_bit1 << 1)
    bank_pipe = pipe + NUM_PIPES * bank
    swizzle = pipe_swizzle + NUM_PIPES * bank_swizzle
    bank_pipe ^= swizzle  # slice 0, sample 0
    bank_pipe %= NUM_PIPES * NUM_BANKS
    pipe = bank_pipe % NUM_PIPES
    bank = bank_pipe // NUM_PIPES
    macro_tile_pitch = 8 * NUM_BANKS
    macro_tile_height = 8 * NUM_PIPES
    if tile_mode in (5, 9):
        macro_tile_pitch >>= 1
        macro_tile_height *= 2
    elif tile_mode in (6, 10):
        macro_tile_pitch >>= 2
        macro_tile_height *= 4
    macro_tiles_per_row = pitch // macro_tile_pitch
    macro_tile_bytes = (thickness * bpp * macro_tile_height * macro_tile_pitch + 7) // 8
    mx = x // macro_tile_pitch
    my = y // macro_tile_height
    macro_tile_offset = (mx + macro_tiles_per_row * my) * macro_tile_bytes
    if tile_mode in (8, 9, 10, 11, 14, 15):
        raise NotImplementedError('bank-swapped tile modes')
    group_mask = (1 << GROUP_BITS) - 1
    num_swizzle_bits = BANK_BITS + PIPE_BITS
    total = elem_offset + (macro_tile_offset >> num_swizzle_bits)
    offset_high = (total & ~group_mask) << num_swizzle_bits
    offset_low = group_mask & total
    return (bank << (PIPE_BITS + GROUP_BITS)) | (pipe << GROUP_BITS) | offset_low | offset_high


def element_offsets(w_el, h_el, bpp, pitch_el, tile_mode=4, swizzle=0):
    """Return array [h_el, w_el] of byte offsets into the swizzled surface for each element."""
    pipe_sw = (swizzle >> 8) & 1
    bank_sw = (swizzle >> 9) & 3
    offs = np.zeros((h_el, w_el), dtype=np.int64)
    for y in range(h_el):
        for x in range(w_el):
            if tile_mode in (0, 1):
                offs[y, x] = (y * pitch_el + x) * (bpp // 8)
            elif tile_mode in (2, 3):
                offs[y, x] = addr_micro_tiled(x, y, bpp, pitch_el, h_el, tile_mode)
            else:
                offs[y, x] = addr_macro_tiled(x, y, bpp, pitch_el, h_el, tile_mode, pipe_sw, bank_sw)
    return offs


def deswizzle(data, w_el, h_el, bpp, pitch_el=None, tile_mode=4, swizzle=0):
    pitch_el = pitch_el or w_el
    bpe = bpp // 8
    offs = element_offsets(w_el, h_el, bpp, pitch_el, tile_mode, swizzle)
    src = np.frombuffer(data, dtype=np.uint8)
    idx = offs[..., None] + np.arange(bpe)[None, None, :]
    return src[idx].reshape(h_el, w_el * bpe).tobytes()


def swizzle_data(linear, w_el, h_el, bpp, size, pitch_el=None, tile_mode=4, swizzle=0):
    pitch_el = pitch_el or w_el
    bpe = bpp // 8
    offs = element_offsets(w_el, h_el, bpp, pitch_el, tile_mode, swizzle)
    out = np.zeros(size, dtype=np.uint8)
    lin = np.frombuffer(linear, dtype=np.uint8).reshape(h_el, w_el, bpe)
    idx = offs[..., None] + np.arange(bpe)[None, None, :]
    out[idx.reshape(-1)] = lin.reshape(-1)
    return out.tobytes()


# ---------------- BC4 ----------------

def bc4_decode(blocks, w, h):
    """blocks: linear BC4 block data (w/4 * h/4 * 8 bytes). Returns uint8 array [h, w]."""
    bw, bh = w // 4, h // 4
    b = np.frombuffer(blocks, dtype=np.uint8).reshape(bh, bw, 8).astype(np.int32)
    a0 = b[..., 0]
    a1 = b[..., 1]
    bits = np.zeros((bh, bw), dtype=np.uint64)
    for i in range(6):
        bits |= b[..., 2 + i].astype(np.uint64) << np.uint64(8 * i)
    pal = np.zeros((bh, bw, 8), dtype=np.int32)
    pal[..., 0] = a0
    pal[..., 1] = a1
    gt = a0 > a1
    for i in range(1, 7):
        v8 = ((7 - i) * a0 + i * a1) // 7
        pal[..., 1 + i] = np.where(gt, v8, pal[..., 1 + i])
    for i in range(1, 5):
        v6 = ((5 - i) * a0 + i * a1) // 5
        pal[..., 1 + i] = np.where(gt, pal[..., 1 + i], v6)
    pal[..., 6] = np.where(gt, pal[..., 6], 0)
    pal[..., 7] = np.where(gt, pal[..., 7], 255)
    out = np.zeros((h, w), dtype=np.uint8)
    for py in range(4):
        for px in range(4):
            sh = np.uint64(3 * (py * 4 + px))
            code = ((bits >> sh) & np.uint64(7)).astype(np.int64)
            val = np.take_along_axis(pal, code[..., None], axis=2)[..., 0]
            out[py::4, px::4] = val
    return out


def bc4_encode(img):
    """img: uint8 [h, w] (multiple of 4). Simple min/max 8-value-palette encoder (high quality for UI glyphs)."""
    h, w = img.shape
    bh, bw = h // 4, w // 4
    blk = img.reshape(bh, 4, bw, 4).transpose(0, 2, 1, 3).reshape(bh, bw, 16).astype(np.int32)
    mx = blk.max(axis=2)
    mn = blk.min(axis=2)
    out = np.zeros((bh, bw, 8), dtype=np.uint8)
    # Use 6-value mode with explicit 0/255 when block contains both extremes and mid values; else 8-value mode.
    a0 = mx.copy()
    a1 = mn.copy()
    same = a0 == a1
    a0 = np.where(same & (a0 < 255), a0 + 1, a0)
    a0 = np.where(same & (a0 == 255) & (a1 == 255), 255, a0)
    a1 = np.where(same & (a1 == 255), 254, a1)
    pal = np.zeros((bh, bw, 8), dtype=np.int32)
    pal[..., 0] = a0
    pal[..., 1] = a1
    for i in range(1, 7):
        pal[..., 1 + i] = ((7 - i) * a0 + i * a1) // 7
    diff = np.abs(blk[..., :, None] - pal[..., None, :])
    codes = diff.argmin(axis=3).astype(np.uint64)
    bits = np.zeros((bh, bw), dtype=np.uint64)
    for i in range(16):
        bits |= codes[..., i] << np.uint64(3 * i)
    out[..., 0] = a0
    out[..., 1] = a1
    for i in range(6):
        out[..., 2 + i] = ((bits >> np.uint64(8 * i)) & np.uint64(0xFF)).astype(np.uint8)
    return out.tobytes()
