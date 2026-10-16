"""Generates Parchment.png (aged paper with a ragged, darkened edge and an alpha border) with the standard library only."""
import math, random, struct, sys, zlib

W, H = 1024, 960
random.seed(7)
out_path = sys.argv[1]


def make_grid(gw, gh):
    return [[random.random() for _ in range(gw + 2)] for _ in range(gh + 2)]


def sample(grid, gw, gh, u, v):
    """bilinear (smoothstep) sample of a value-noise grid, u and v in 0..1"""
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


layers = {
    'big': (5, 5, make_grid(5, 5)),
    'mid': (14, 13, make_grid(14, 13)),
    'fine': (60, 56, make_grid(60, 56)),
    'grain': (220, 205, make_grid(220, 205)),
    'edge_lo': (9, 9, make_grid(9, 9)),
    'edge_hi': (70, 66, make_grid(70, 66)),
    'stain': (4, 4, make_grid(4, 4)),
}

BASE = (222.0, 214.0, 199.0)
EDGE = (116.0, 98.0, 76.0)
rows = []
for y in range(H):
    v = y / (H - 1)
    row = bytearray([0])
    for x in range(W):
        u = x / (W - 1)
        gw, gh, g = layers['big']
        big = sample(g, gw, gh, u, v) - 0.5
        gw, gh, g = layers['mid']
        mid = sample(g, gw, gh, u, v) - 0.5
        gw, gh, g = layers['fine']
        fine = sample(g, gw, gh, u, v) - 0.5
        gw, gh, g = layers['grain']
        grain = sample(g, gw, gh, u, v) - 0.5
        gw, gh, g = layers['stain']
        stain = sample(g, gw, gh, u, v)
        # paper: broad blotches, mid mottling, fine tooth
        shade = big * 26 + mid * 16 + fine * 9 + grain * 7
        r = BASE[0] + shade
        gcol = BASE[1] + shade * 0.97
        b = BASE[2] + shade * 0.85
        # a few warm stains
        if stain > 0.62:
            k = (stain - 0.62) * 0.9
            r -= 12 * k
            gcol -= 14 * k
            b -= 20 * k
        # ragged edge: the distance to the nearest side, perturbed by noise
        d = min(x, W - 1 - x, y, H - 1 - y)
        gw, gh, g = layers['edge_lo']
        lo = sample(g, gw, gh, u, v) - 0.5
        gw, gh, g = layers['edge_hi']
        hi = sample(g, gw, gh, u, v) - 0.5
        dn = d - (16 + lo * 26 + hi * 16)
        alpha = max(0.0, min(1.0, dn / 3.0))
        # darkened, browner border inside the edge
        burn = max(0.0, 1.0 - dn / 70.0) ** 1.8
        r = r * (1 - 0.55 * burn) + EDGE[0] * 0.55 * burn
        gcol = gcol * (1 - 0.55 * burn) + EDGE[1] * 0.55 * burn
        b = b * (1 - 0.55 * burn) + EDGE[2] * 0.55 * burn
        row += bytes((max(0, min(255, int(r))), max(0, min(255, int(gcol))), max(0, min(255, int(b))), int(alpha * 255)))
    rows.append(bytes(row))


def chunk(tag, data):
    c = struct.pack('>I', len(data)) + tag + data
    return c + struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF)


png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(b''.join(rows), 9)) + chunk(b'IEND', b'')
open(out_path, 'wb').write(png)
print('wrote', out_path, len(png), 'bytes')
