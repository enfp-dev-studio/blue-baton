# SPDX-License-Identifier: GPL-3.0-only
"""Draws the Blue Baton icons as SVG and rasterizes them with headless Chrome (no image libraries).

  assets/icon.svg, assets/icon-1024.png   app icon (tauri icon makes the platform sizes from the PNG)
  assets/tray.svg, src-tauri/icons/tray.png   menu bar template icon: black on transparent, macOS tints it

The mark, flat black and white: a relay baton (a tube, its open end showing) passed at speed, two speed lines
behind it. App icon on the macOS grid: an 824/1024 rounded square (superellipse) on 1024.
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


def baton(fill, hole):
    """The baton along the x axis, centred on (512, 512) before the tilt: a tube seen from the side, its leading end
    open (a ring with the hole in it), its back end the outer half of the same ellipse, two speed lines behind it.
    Flat: one colour for everything, the hole in the background colour"""
    return f'''<rect x="186" y="437" width="112" height="62" rx="31" fill="{fill}"/>
    <rect x="156" y="525" width="142" height="62" rx="31" fill="{fill}"/>
    <path d="M392 428 H800 V596 H392 A40 84 0 0 1 392 428 Z" fill="{fill}"/>
    <ellipse cx="800" cy="512" rx="40" ry="84" fill="{fill}"/>
    <ellipse cx="800" cy="512" rx="22" ry="54" fill="{hole}"/>'''


def app_svg():
    # Flat black and white on the macOS grid: an 824/1024 superellipse tile
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="1024" viewBox="0 0 1024 1024">
  <path d="{squircle(512, 512, 412)}" fill="#000"/>
  <g transform="rotate({ANGLE} 512 512)">
    {baton('#fff', '#000')}
  </g>
</svg>
'''


def tray_svg():
    # 22 x 22 pt menu bar icon, drawn on a 44 grid. Template image: only the alpha matters, so the hole is cut out
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="22" height="22" viewBox="0 0 44 44">
  <g transform="rotate({ANGLE} 22 22)">
    <rect x="4" y="18.5" width="5" height="3" rx="1.5"/>
    <rect x="3" y="23" width="6" height="3" rx="1.5"/>
    <path fill-rule="evenodd" d="M15 16 H35 A3 6 0 0 1 35 28 H15 A3 6 0 0 1 15 16 Z M35 18.6 A1.4 3.4 0 0 0 35 25.4 A1.4 3.4 0 0 0 35 18.6 Z"/>
  </g>
</svg>
'''


def rasterize(svg_path, png_path, size, scale=1):
    with tempfile.TemporaryDirectory() as profile:
        subprocess.run([CHROME, '--headless', '--disable-gpu', '--hide-scrollbars', f'--user-data-dir={profile}',
                        '--default-background-color=00000000', f'--force-device-scale-factor={scale}',
                        f'--window-size={size},{size}', f'--screenshot={png_path}', 'file://' + svg_path],
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
    print('icons written')
