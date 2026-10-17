"""Generates SearchBarYours.png (green) and SearchBarTheirs.png (red): the container / barter menu's search bars. Like SearchBar.png - a worn, slightly jagged
panel with a fine woven grain - but tinted, and with a golden band inside the worn edge so the bar reads against the dark menu frame.
Usage: gen_searchbar_side.py <out.png> green|red   (standard library only)"""
import math, random, struct, sys, zlib

W, H = 1652, 74          # twice the bar's 826 x 37
out_path = sys.argv[1]
kind = sys.argv[2]
random.seed(31 if kind == 'green' else 47)

# base tint of the panel (r, g, b multipliers of the shade) and the gold of the band
if kind == 'green':
    TINT = (0.55, 1.55, 0.62)
else:
    TINT = (1.75, 0.50, 0.50)
GOLD = (206, 164, 74)


def make_grid(gw, gh):
    return [[random.random() for _ in range(gw + 2)] for _ in range(gh + 2)]


def sample(grid, gw, gh, u, v):
    x = u * gw
    y = v * gh
    xi = int(x)
    yi = int(y)
    fx = x - xi
    fy = y - yi
    fx = fx * fx * (3 - 2 * fx)
    fy = fy * fy * (3 - 2 * fy)
    a = grid[yi][xi]
    b = grid[yi][xi + 1]
    c = grid[yi + 1][xi]
    d = grid[yi + 1][xi + 1]
    return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy


blot = (14, 3, make_grid(14, 3))
grain = (440, 22, make_grid(440, 22))
edge_n = (70, 6, make_grid(70, 6))
band_n = (60, 6, make_grid(60, 6))
gold_n = (300, 10, make_grid(300, 10))
scratches = []
for _ in range(10):
    x0 = random.uniform(0, W)
    y0 = random.uniform(0, H)
    ang = random.uniform(-0.5, 0.5)
    ln = random.uniform(80, 360)
    scratches.append((x0, y0, math.cos(ang), math.sin(ang), ln, random.uniform(2.0, 5.0)))

rows = []
for y in range(H):
    v = y / (H - 1)
    row = bytearray([0])
    for x in range(W):
        u = x / (W - 1)
        gw, gh, g = blot
        b1 = sample(g, gw, gh, u, v) - 0.5
        gw, gh, g = grain
        g1 = sample(g, gw, gh, u, v) - 0.5
        shade = 22 + b1 * 8 + g1 * 9
        shade += ((x // 3 + y // 3) % 2) * 2.5 - 1.25
        shade += 3.0 * math.sin(math.pi * v)
        for sx, sy, cx, cy, ln, amp in scratches:
            t = (x - sx) * cx + (y - sy) * cy
            if 0 <= t <= ln:
                d = abs((x - sx) * cy - (y - sy) * cx)
                if d < 0.9:
                    shade += amp * (1 - d) * (1 - t / ln)
        r, gcol, b = shade * TINT[0], shade * TINT[1], shade * TINT[2]

        d = min(x, W - 1 - x, y, H - 1 - y)
        gw, gh, g = edge_n
        en = sample(g, gw, gh, u, v) - 0.5
        gw, gh, g = band_n
        bn = sample(g, gw, gh, u, v) - 0.5
        # the golden band: from the worn outer edge in, about 6 px, its inner line wavering a little; worn gold, not flat
        outer = 2.0 + en * 2.0
        inner = outer + 6.0 + bn * 2.4
        if outer <= d < inner:
            gw, gh, g = gold_n
            gn = sample(g, gw, gh, u, v) - 0.5
            k = 0.78 + gn * 0.5 + 0.12 * math.sin(math.pi * v)
            # a darker thread on the band's inner side, a lighter one on its outer
            t = (d - outer) / max(1.0, inner - outer)
            if t > 0.8:
                k *= 0.7
            elif t < 0.2:
                k *= 1.12
            r, gcol, b = GOLD[0] * k, GOLD[1] * k, GOLD[2] * k
        elif d >= inner:
            # a little shadow under the band
            fall = max(0.0, 1.0 - (d - inner) / 5.0)
            r, gcol, b = r * (1 - 0.35 * fall), gcol * (1 - 0.35 * fall), b * (1 - 0.35 * fall)
        # worn, uneven outer edge: the alpha fades over the outer pixels along a wavering line; the corners stay square
        dn = d - (1.0 + en * 2.0)
        alpha = max(0.0, min(1.0, dn / 2.5))
        row += bytes((max(0, min(255, int(r))), max(0, min(255, int(gcol))), max(0, min(255, int(b))), int(alpha * 255)))
    rows.append(bytes(row))


def chunk(tag, data):
    c = struct.pack('>I', len(data)) + tag + data
    return c + struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF)


png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(b''.join(rows), 9)) + chunk(b'IEND', b'')
open(out_path, 'wb').write(png)
print('wrote', out_path, len(png), 'bytes')
