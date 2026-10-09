"""Draws the Blue Baton icons without image libraries (signed distance shapes, 4x4 supersampling).

  assets/icon-1024.png        app icon source (tauri icon makes the platform sizes from it)
  src-tauri/icons/tray.png    menu bar template icon: black shapes on transparent, macOS tints it
The mark: a baton (capsule) passed between two hands (dots), as in the settings UI header.
"""
import math, os, struct, zlib

HERE = os.path.dirname(os.path.abspath(__file__))


def png(path, w, h, rgba):
    raw = b''.join(b'\x00' + bytes(rgba[y * w * 4:(y + 1) * w * 4]) for y in range(h))
    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    data = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
    data += chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(data)


def rrect(px, py, cx, cy, hw, hh, r):
    qx, qy = abs(px - cx) - hw + r, abs(py - cy) - hh + r
    return math.hypot(max(qx, 0), max(qy, 0)) + min(max(qx, qy), 0) - r


def capsule(px, py, cx, cy, half_len, r, angle):
    c, s = math.cos(angle), math.sin(angle)
    x, y = (px - cx) * c + (py - cy) * s, -(px - cx) * s + (py - cy) * c
    x = max(abs(x) - half_len, 0)
    return math.hypot(x, y) - r


def circle(px, py, cx, cy, r):
    return math.hypot(px - cx, py - cy) - r


def render(size, layers, ss=4):
    """layers: list of (sdf(x, y) in 0..32 units, color(x, y) -> (r, g, b, a))"""
    out = bytearray(size * size * 4)
    k = 32.0 / size
    for j in range(size):
        for i in range(size):
            acc = [0.0, 0.0, 0.0, 0.0]
            for sy in range(ss):
                for sx in range(ss):
                    x, y = (i + (sx + 0.5) / ss) * k, (j + (sy + 0.5) / ss) * k
                    r = g = b = a = 0.0
                    for sdf, color in layers:
                        if sdf(x, y) <= 0:
                            cr, cg, cb, ca = color(x, y)
                            r, g, b = r * (1 - ca) + cr * ca, g * (1 - ca) + cg * ca, b * (1 - ca) + cb * ca
                            a = a + ca * (1 - a)
                    acc[0] += r; acc[1] += g; acc[2] += b; acc[3] += a
            n = ss * ss
            a = acc[3] / n
            o = (j * size + i) * 4
            if a > 0:
                out[o:o + 4] = bytes([min(255, round(acc[0] / n / a * 255)) if a else 0,
                                      min(255, round(acc[1] / n / a * 255)) if a else 0,
                                      min(255, round(acc[2] / n / a * 255)) if a else 0, round(a * 255)])
    return out


ANGLE = math.radians(-32)
LIGHT, DEEP = (0x7c / 255, 0x9c / 255, 1.0), (0x2b / 255, 0x59 / 255, 0xf5 / 255)


def gradient(x, y):
    t = min(1, max(0, (x + y) / 64))
    return tuple(LIGHT[i] + (DEEP[i] - LIGHT[i]) * t for i in range(3)) + (1.0,)


def white(alpha):
    return lambda x, y: (1.0, 1.0, 1.0, alpha)


AX, AY = math.cos(ANGLE), math.sin(ANGLE)


def along(d):
    """A point on the baton's axis, d units from the center"""
    return 16 + d * AX, 16 + d * AY


# macOS icon grid: the rounded square is 824 of 1024 (12.9 of 16), corner about 22% of its size
D1, D2 = along(9.2), along(-9.2)
app_layers = [
    (lambda x, y: rrect(x, y, 16, 16, 12.9, 12.9, 5.8), gradient),
    (lambda x, y: capsule(x, y, 16, 16, 4.6, 2.1, ANGLE), white(1.0)),
    (lambda x, y: circle(x, y, D2[0], D2[1], 1.5), white(0.6)),
    (lambda x, y: circle(x, y, D1[0], D1[1], 1.5), white(1.0)),
]
png(os.path.join(HERE, 'icon-1024.png'), 1024, 1024, render(1024, app_layers, ss=2))

black = lambda x, y: (0.0, 0.0, 0.0, 1.0)
T1, T2 = along(13.4), along(-13.4)
tray_layers = [
    (lambda x, y: capsule(x, y, 16, 16, 6.5, 3.0, ANGLE), black),
    (lambda x, y: circle(x, y, T2[0], T2[1], 2.3), lambda x, y: (0.0, 0.0, 0.0, 0.55)),
    (lambda x, y: circle(x, y, T1[0], T1[1], 2.3), black),
]
png(os.path.join(HERE, '..', 'src-tauri', 'icons', 'tray.png'), 44, 44, render(44, tray_layers))
print('icons written')
