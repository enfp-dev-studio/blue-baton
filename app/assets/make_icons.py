"""Draws the Blue Baton icons as SVG and rasterizes them with headless Chrome (no image libraries).

  assets/icon.svg, assets/icon-1024.png   app icon (tauri icon makes the platform sizes from the PNG)
  assets/tray.svg, src-tauri/icons/tray.png   menu bar template icon: black on transparent, macOS tints it

The mark: a hollow relay baton passed at speed (streaks behind it), the same baton that slides between the
hosts in the settings UI. App icon on the macOS grid: an 824/1024 rounded square (superellipse) on 1024.
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


ANGLE = -33   # baton tilt, degrees


def app_svg():
    # The baton is drawn along the x axis around (0, 0), then rotated and placed
    L, R = 230, 64          # half length, tube radius
    cap = 0.36 * R          # ellipse width of the open end (perspective)
    streaks = ''.join(
        f'<rect x="{-L - gap - ln}" y="{y - w / 2}" width="{ln}" height="{w}" rx="{w / 2}" fill="url(#streak)" opacity="{op}"/>'
        for y, ln, w, gap, op in [(-40, 150, 22, 34, 0.9), (4, 210, 24, 30, 1.0), (46, 120, 20, 40, 0.75)])
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="1024" viewBox="0 0 1024 1024">
  <defs>
    <linearGradient id="bg" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#26357a"/>
      <stop offset="0.55" stop-color="#141c47"/>
      <stop offset="1" stop-color="#0b1030"/>
    </linearGradient>
    <radialGradient id="sheen" cx="0.3" cy="0.12" r="0.75">
      <stop offset="0" stop-color="#6f8cff" stop-opacity="0.55"/>
      <stop offset="1" stop-color="#6f8cff" stop-opacity="0"/>
    </radialGradient>
    <linearGradient id="tube" x1="0" y1="{-R}" x2="0" y2="{R}" gradientUnits="userSpaceOnUse">
      <stop offset="0" stop-color="#c9d6ff"/>
      <stop offset="0.18" stop-color="#8aa6ff"/>
      <stop offset="0.5" stop-color="#3d6bff"/>
      <stop offset="0.85" stop-color="#2242d6"/>
      <stop offset="1" stop-color="#1a32a8"/>
    </linearGradient>
    <linearGradient id="rim" x1="0" y1="{-R}" x2="0" y2="{R}" gradientUnits="userSpaceOnUse">
      <stop offset="0" stop-color="#eef2ff"/>
      <stop offset="1" stop-color="#7d97ff"/>
    </linearGradient>
    <linearGradient id="hole" x1="0" y1="{-R}" x2="0" y2="{R}" gradientUnits="userSpaceOnUse">
      <stop offset="0" stop-color="#0c1440"/>
      <stop offset="1" stop-color="#2a45c4"/>
    </linearGradient>
    <linearGradient id="streak" x1="1" y1="0" x2="0" y2="0">
      <stop offset="0" stop-color="#9db3ff" stop-opacity="0.95"/>
      <stop offset="1" stop-color="#9db3ff" stop-opacity="0"/>
    </linearGradient>
    <filter id="glow" x="-50%" y="-50%" width="200%" height="200%">
      <feGaussianBlur stdDeviation="34"/>
    </filter>
    <clipPath id="shape"><path d="{squircle(512, 512, 412)}"/></clipPath>
  </defs>
  <g clip-path="url(#shape)">
    <rect width="1024" height="1024" fill="url(#bg)"/>
    <rect width="1024" height="1024" fill="url(#sheen)"/>
    <g transform="translate(584 478) rotate({ANGLE})">
      <rect x="{-L}" y="{-R * 0.9}" width="{2 * L}" height="{1.8 * R}" rx="{R * 0.9}" fill="#3d6bff" opacity="0.75" filter="url(#glow)" transform="translate(0 40)"/>
      {streaks}
      <ellipse cx="{-L}" cy="0" rx="{cap}" ry="{R}" fill="url(#tube)"/>
      <rect x="{-L}" y="{-R}" width="{2 * L}" height="{2 * R}" fill="url(#tube)"/>
      <rect x="{-L + 36}" y="{-R * 0.62}" width="{2 * L - 90}" height="{R * 0.2}" rx="{R * 0.1}" fill="#ffffff" opacity="0.7"/>
      <ellipse cx="{L}" cy="0" rx="{cap}" ry="{R}" fill="url(#rim)"/>
      <ellipse cx="{L + 2}" cy="0" rx="{cap * 0.62}" ry="{R * 0.7}" fill="url(#hole)"/>
    </g>
    <path d="{squircle(512, 512, 412)}" fill="none" stroke="#ffffff" stroke-opacity="0.12" stroke-width="4"/>
  </g>
</svg>
'''


def tray_svg():
    # 22 x 22 pt menu bar icon. Template image: only the alpha matters
    L, R = 5.6, 2.5
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="22" height="22" viewBox="0 0 22 22">
  <defs>
    <mask id="cut">
      <rect x="-20" y="-20" width="40" height="40" fill="#fff"/>
      <ellipse cx="{L}" cy="0" rx="0.75" ry="{R * 0.62}" fill="#000"/>
    </mask>
  </defs>
  <g transform="translate(12.4 10.2) rotate({ANGLE})">
    <g mask="url(#cut)">
      <rect x="{-L}" y="{-R}" width="{L}" height="{2 * R}" rx="1.1" fill="#000"/>
      <rect x="-1" y="{-R}" width="{L + 1}" height="{2 * R}" fill="#000"/>
      <ellipse cx="{L}" cy="0" rx="1.1" ry="{R}" fill="#000"/>
    </g>
    <rect x="{-L - 5.6}" y="-2.05" width="4.2" height="1.5" rx="0.75" fill="#000" opacity="0.8"/>
    <rect x="{-L - 4.4}" y="0.75" width="3.0" height="1.5" rx="0.75" fill="#000" opacity="0.6"/>
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
