# SPDX-License-Identifier: GPL-3.0-only
"""Draws the Blue Baton icons as SVG and rasterizes them with headless Chrome (no image libraries).

  assets/icon.svg, assets/icon-1024.png   app icon (tauri icon makes the platform sizes from the PNG)
  assets/tray.svg, src-tauri/icons/tray.png   menu bar template icon: black on transparent, macOS tints it
  src-tauri/icons/tray-group.png, tray-cell-{1,2,3}-{on,off}.png   macOS menu bar while the board is connected: a
      rounded outline holding a smaller baton, and the three host cells that src-tauri/src/tray.rs puts inside it
      (at GROUP_CELL_X), the active one filled, the others outlined

The mark, flat black and white with one blue band: a relay baton, a tube seen from the side with its open end
showing, filling the tile. The menu bar icon is the same baton in one colour, the band as two thin cuts. App icon on the macOS grid: an 824/1024 rounded square (superellipse) on 1024.
Chrome: $CHROME, or the default macOS install path.
"""
import math, os, subprocess, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
CHROME = os.environ.get('CHROME', '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome')


def squircle(cx, cy, half, n=5.0, steps=256):
    """Superellipse |x|^n + |y|^n = half^n, close to Apple's continuous-corner icon shape"""
    pts = []
    for k in range(steps):
        t = 2 * math.pi * k / steps
        c, s = math.cos(t), math.sin(t)
        x = half * math.copysign(abs(c) ** (2 / n), c)
        y = half * math.copysign(abs(s) ** (2 / n), s)
        pts.append(f'{cx + x:.2f},{cy + y:.2f}')
    return 'M' + ' L'.join(pts) + ' Z'


ANGLE = -35   # baton tilt, degrees


BLUE = '#3d6bff'   # the band: the one colour on the black-and-white mark


def baton(fill, hole, band):
    """The baton along the x axis, centred on (512, 512) before the tilt: a tube seen from the side, its leading end
    open (a ring with the hole in it), its back end the outer half of the same ellipse, a coloured band around it.
    Seen from the open end, so the band's edges curve the way the back end does. Flat: no gradients or shadows"""
    return f'''<path d="M219 392 H805 V632 H219 A57 120 0 0 1 219 392 Z" fill="{fill}"/>
    <path d="M370 392 A57 120 0 0 0 370 632 H450 A57 120 0 0 1 450 392 Z" fill="{band}"/>
    <ellipse cx="805" cy="512" rx="57" ry="120" fill="{fill}"/>
    <ellipse cx="805" cy="512" rx="32" ry="78" fill="{hole}"/>'''


def app_svg():
    # Flat black and white on the macOS grid: an 824/1024 superellipse tile
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="1024" viewBox="0 0 1024 1024">
  <path d="{squircle(512, 512, 412)}" fill="#000"/>
  <g transform="rotate({ANGLE} 512 512)">
    {baton('#fff', '#000', BLUE)}
  </g>
</svg>
'''


def tray_body():
    # The menu bar baton on a 44 grid. Template image: only the alpha matters, so the hole is cut out. One colour
    # only, so the band shows as its two edges cut out of the baton
    return f'''<defs>
    <mask id="band">
      <rect x="-10" y="-10" width="64" height="64" fill="#fff"/>
      <path d="M14.8 15.5 A3.2 6.5 0 0 0 14.8 28.5 M18.9 15.5 A3.2 6.5 0 0 0 18.9 28.5" fill="none" stroke="#000" stroke-width="1.3"/>
    </mask>
  </defs>
  <g transform="rotate({ANGLE} 22 22)">
    <path mask="url(#band)" fill-rule="evenodd" d="M7.2 15.5 H36.8 A3.2 6.5 0 0 1 36.8 28.5 H7.2 A3.2 6.5 0 0 1 7.2 15.5 Z M36.8 18.2 A1.6 3.8 0 0 0 36.8 25.8 A1.6 3.8 0 0 0 36.8 18.2 Z"/>
  </g>'''


def tray_svg():
    # 22 x 22 pt menu bar icon
    return f'<svg xmlns="http://www.w3.org/2000/svg" width="22" height="22" viewBox="0 0 44 44">\n  {tray_body()}\n</svg>\n'


# The group (pt, in a menu bar slot 22 high): a rounded outline, the baton in a 15 pt box, three 10 x 12 cells
GROUP_W, GROUP_H = 54, 22
GROUP_CELL_X = (17.5, 29.5, 41.5)   # tray.rs: the same at 2x (35, 59, 83 px)
CELL_W = 10


def group_svg():
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{GROUP_W}" height="{GROUP_H}" viewBox="0 0 {GROUP_W} {GROUP_H}">'
            f'<rect x="0.6" y="2.6" width="{GROUP_W - 1.2}" height="16.8" rx="4.4" fill="none" stroke="#000" stroke-width="1.2"/>'
            f'<svg x="2" y="3.5" width="15" height="15" viewBox="0 0 44 44">{tray_body()}</svg></svg>\n')


def cell_svg(n, on):
    # One host cell: filled with the digit cut out (active), or an outline with the digit (the others; tray.rs fades
    # an empty or disconnected one)
    digit = (f'<text x="5" y="11.3" text-anchor="middle" dominant-baseline="central" font-size="8.5" font-weight="700" '
             f'font-family="-apple-system, system-ui, Helvetica, sans-serif">{n}</text>')
    if on:
        body = (f'<mask id="d"><rect width="{CELL_W}" height="{GROUP_H}" fill="#fff"/><g fill="#000">{digit}</g></mask>'
                f'<rect x="0" y="5" width="{CELL_W}" height="12" rx="2.5" mask="url(#d)"/>')
    else:
        body = (f'<rect x="0.5" y="5.5" width="{CELL_W - 1}" height="11" rx="2" fill="none" stroke="#000" '
                f'stroke-width="1"/>{digit}')
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{CELL_W}" height="{GROUP_H}" viewBox="0 0 {CELL_W} {GROUP_H}">'
            f'{body}</svg>\n')


def rasterize(svg_path, png_path, size, scale=1, height=None):
    with tempfile.TemporaryDirectory() as profile:
        subprocess.run([CHROME, '--headless', '--disable-gpu', '--hide-scrollbars', f'--user-data-dir={profile}',
                        '--default-background-color=00000000', f'--force-device-scale-factor={scale}',
                        f'--window-size={size},{height or size}', f'--screenshot={png_path}', 'file://' + svg_path],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def write(path, text):
    with open(path, 'w') as f:
        f.write(text)


if __name__ == '__main__':
    app, tray = os.path.join(HERE, 'icon.svg'), os.path.join(HERE, 'tray.svg')
    write(app, app_svg())
    write(tray, tray_svg())
    rasterize(app, os.path.join(HERE, 'icon-1024.png'), 1024)
    rasterize(tray, os.path.join(HERE, '..', 'src-tauri', 'icons', 'tray.png'), 22, scale=2)
    with tempfile.TemporaryDirectory() as tmp:
        svg = os.path.join(tmp, 'tray-group.svg')
        write(svg, group_svg())
        rasterize(svg, os.path.join(HERE, '..', 'src-tauri', 'icons', 'tray-group.png'), GROUP_W, scale=2, height=GROUP_H)
        for n in (1, 2, 3):
            for on in (True, False):
                name = f'tray-cell-{n}-{"on" if on else "off"}'
                svg = os.path.join(tmp, name + '.svg')
                write(svg, cell_svg(n, on))
                rasterize(svg, os.path.join(HERE, '..', 'src-tauri', 'icons', name + '.png'), CELL_W, scale=2,
                          height=GROUP_H)
    print('icons written')
