"""Generates SearchBar.png: a near-black, slightly worn panel with a fine woven grain, standard library only."""
import math, random, struct, sys, zlib

W, H = 972, 72          # twice the bar's 486 x 36
random.seed(11)
out_path = sys.argv[1]


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


blot = (9, 3, make_grid(9, 3))
grain = (260, 22, make_grid(260, 22))
edge_n = (40, 6, make_grid(40, 6))
scratches = []
for _ in range(7):
    x0 = random.uniform(0, W)
    y0 = random.uniform(0, H)
    ang = random.uniform(-0.5, 0.5)
    ln = random.uniform(60, 220)
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
        # base: near black with a faint warm cast, soft blotches and fine grain
        shade = 20 + b1 * 7 + g1 * 8
        # a fine woven lattice (the dark frames around the menus have one)
        lattice = ((x // 3 + y // 3) % 2) * 2.5 - 1.25
        shade += lattice
        # darker towards the top and the bottom, lighter in the middle band
        shade += 3.0 * math.sin(math.pi * v)
        # faint scratches
        for sx, sy, cx, cy, ln, amp in scratches:
            t = (x - sx) * cx + (y - sy) * cy
            if 0 <= t <= ln:
                d = abs((x - sx) * cy - (y - sy) * cx)
                if d < 0.9:
                    shade += amp * (1 - d) * (1 - t / ln)
        r = shade * 1.02
        gcol = shade * 0.99
        b = shade * 0.95
        # a thin, slightly lighter inner edge
        d = min(x, W - 1 - x, y, H - 1 - y)
        gw, gh, g = edge_n
        en = sample(g, gw, gh, u, v) - 0.5
        # worn, uneven outer edge (as before): the alpha fades over the outer pixels along a wavering line; the corners stay square
        dn = d - (1.5 + en * 2.0)
        alpha = max(0.0, min(1.0, dn / 2.5))
        row += bytes((max(0, min(255, int(r))), max(0, min(255, int(gcol))), max(0, min(255, int(b))), int(alpha * 255)))
    rows.append(bytes(row))


def chunk(tag, data):
    c = struct.pack('>I', len(data)) + tag + data
    return c + struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF)


png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(b''.join(rows), 9)) + chunk(b'IEND', b'')
open(out_path, 'wb').write(png)
print('wrote', out_path, len(png), 'bytes')
