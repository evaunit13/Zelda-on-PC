"""Draws the application icon (original artwork: a keyboard key with a "Z" on a teal tile).

Every size is rendered on its own from vector shapes (no downscaled bitmap), so the small sizes stay crisp.
Usage: python tools/make_app_icon.py            (writes src/resource/zelda_on_pc.ico, zelda_on_pc16.ico, docs/icon.png)
"""
import os

from PIL import Image, ImageDraw

SIZES = [256, 128, 96, 64, 48, 40, 32, 24, 20, 16]
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(len(a)))


def render(size):
    ss = 8 if size <= 48 else 4  # supersampling
    n = size * ss
    img = Image.new('RGBA', (n, n), (0, 0, 0, 0))

    # background tile: vertical gradient, rounded corners
    top, bottom = (22, 104, 112), (12, 38, 58)
    grad = Image.new('RGBA', (n, n))
    gd = ImageDraw.Draw(grad)
    for y in range(n):
        gd.line([(0, y), (n, y)], fill=lerp(top, bottom, y / (n - 1)) + (255,))
    mask = Image.new('L', (n, n), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, n - 1, n - 1], radius=int(n * 0.20), fill=255)
    img.paste(grad, (0, 0), mask)
    d = ImageDraw.Draw(img)

    # key: a darker base (the side of the key) with the lighter cap on top
    small = size <= 24
    m = n * (0.13 if small else 0.16)
    depth = n * (0.07 if small else 0.085)
    r = n * 0.13
    d.rounded_rectangle([m, m + depth, n - m, n - m + depth * 0.35], radius=r, fill=(150, 160, 162, 255))
    d.rounded_rectangle([m, m - depth * 0.35, n - m, n - m - depth * 0.65], radius=r, fill=(244, 244, 236, 255))

    # the letter, drawn as one polygon (two bars and a diagonal)
    cx, cy = n / 2, (m - depth * 0.35 + n - m - depth * 0.65) / 2
    w = n * (0.36 if small else 0.33)
    h = n * (0.38 if small else 0.35)
    t = n * (0.085 if small else 0.072)  # stroke
    x0, x1, y0, y1 = cx - w / 2, cx + w / 2, cy - h / 2, cy + h / 2
    k = t * 1.35  # horizontal extent of the diagonal's cut
    ink = (14, 44, 62, 255)
    d.polygon([(x0, y0), (x1, y0), (x1, y0 + t), (x0 + k, y1 - t), (x1, y1 - t), (x1, y1), (x0, y1), (x0, y1 - t),
               (x1 - k, y0 + t), (x0, y0 + t)], fill=ink)
    return img.resize((size, size), Image.LANCZOS)


def main():
    imgs = [render(s) for s in SIZES]
    res = os.path.join(ROOT, 'src', 'resource')
    imgs[0].save(os.path.join(res, 'zelda_on_pc.ico'), format='ICO', sizes=[(s, s) for s in SIZES], append_images=imgs[1:])
    imgs[-1].save(os.path.join(res, 'zelda_on_pc16.ico'), format='ICO', sizes=[(16, 16)])
    os.makedirs(os.path.join(ROOT, 'docs'), exist_ok=True)
    imgs[0].save(os.path.join(ROOT, 'docs', 'icon.png'))


if __name__ == '__main__':
    main()
