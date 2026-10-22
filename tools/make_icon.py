"""SKYFALL application icon (original artwork): a twin-tail jet climbing out of a dusk sky.

    python3 tools/make_icon.py packaging/icon/SKYFALL.png packaging/icon/SKYFALL.icns
"""
import math, sys
from PIL import Image, ImageDraw, ImageFilter

S = 2048  # draw at 2x, downsample for smooth edges

def lerp(a, b, t): return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))

def squircle_mask(size, r_frac=0.225):
    m = Image.new("L", (size, size), 0)
    d = ImageDraw.Draw(m)
    inset = int(size * 0.098)          # Apple grid: 824/1024 content
    d.rounded_rectangle([inset, inset, size - inset, size - inset], radius=int((size - 2 * inset) * r_frac), fill=255)
    return m

def background(size):
    img = Image.new("RGB", (size, size))
    px = img.load()
    top, mid, hor = (10, 18, 38), (34, 52, 92), (232, 132, 64)
    for y in range(size):
        t = y / size
        if t < 0.62: c = lerp(top, mid, t / 0.62)
        else: c = lerp(mid, hor, min(1.0, (t - 0.62) / 0.3) ** 1.4)
        for x in range(size):
            px[x, y] = c
    # sun glow on the horizon
    glow = Image.new("L", (size, size), 0)
    gd = ImageDraw.Draw(glow)
    cx, cy = int(size * 0.7), int(size * 0.86)
    for r in range(int(size * 0.55), 0, -8):
        a = int(255 * (1 - r / (size * 0.55)) ** 2.2)
        gd.ellipse([cx - r, cy - r * 0.55, cx + r, cy + r * 0.55], fill=a)
    sun = Image.new("RGB", (size, size), (255, 214, 150))
    img = Image.composite(sun, img, glow)
    # distant mountains
    d = ImageDraw.Draw(img)
    pts = [(0, size)]
    import random
    random.seed(7)
    x = 0
    y = size * 0.83
    while x <= size:
        pts.append((x, y))
        x += size * 0.05
        y = size * 0.83 + math.sin(x / size * 9.0) * size * 0.025 + random.uniform(-1, 1) * size * 0.012
    pts.append((size, size))
    d.polygon(pts, fill=(22, 26, 40))
    return img

def jet(size):
    # original twin-tail fighter planform, nose up-right (climbing); unit coords, nose at +x
    body = [(1.00, 0.00), (0.80, 0.045), (0.55, 0.075), (0.30, 0.09), (0.10, 0.36), (-0.12, 0.40), (-0.20, 0.30),
            (-0.18, 0.12), (-0.42, 0.12), (-0.52, 0.30), (-0.66, 0.30), (-0.62, 0.09), (-0.70, 0.07),
            (-0.70, -0.07), (-0.62, -0.09), (-0.66, -0.30), (-0.52, -0.30), (-0.42, -0.12), (-0.18, -0.12),
            (-0.20, -0.30), (-0.12, -0.40), (0.10, -0.36), (0.30, -0.09), (0.55, -0.075), (0.80, -0.045)]
    canopy = [(0.78, 0.0), (0.66, 0.035), (0.50, 0.04), (0.42, 0.0), (0.50, -0.04), (0.66, -0.035)]
    spine = [(0.42, 0.012), (-0.55, 0.02), (-0.55, -0.02), (0.42, -0.012)]
    return body, canopy, spine

def main(out_png, out_icns):
    img = background(S).convert("RGBA")
    ang = math.radians(-34)   # climbing to the upper right
    cx, cy, scale = S * 0.50, S * 0.50, S * 0.36
    def tf(p):
        x, y = p
        return (cx + (x * math.cos(ang) - y * math.sin(ang)) * scale, cy + (x * math.sin(ang) + y * math.cos(ang)) * scale)
    body, canopy, spine = jet(S)
    # contrails / afterburner
    trail = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    td = ImageDraw.Draw(trail)
    for off, w in ((0.045, 0.030), (-0.045, 0.030)):
        a0 = tf((-0.70, off)); a1 = tf((-2.2, off * 3))
        for k in range(40):
            t = k / 39
            p = (a0[0] + (a1[0] - a0[0]) * t, a0[1] + (a1[1] - a0[1]) * t)
            r = S * (w * (0.5 + t * 1.6))
            alpha = int(150 * (1 - t) ** 1.5)
            td.ellipse([p[0] - r, p[1] - r, p[0] + r, p[1] + r], fill=(255, 236, 210, alpha))
    trail = trail.filter(ImageFilter.GaussianBlur(S * 0.012))
    img = Image.alpha_composite(img, trail)
    # afterburner cores
    burn = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    bd = ImageDraw.Draw(burn)
    for off in (0.045, -0.045):
        for k in range(18):
            t = k / 17
            p = tf((-0.72 - t * 0.38, off * (1 + t * 0.3)))
            r = S * 0.028 * (1 - t * 0.6)
            col = (120, 190, 255, int(230 * (1 - t))) if t < 0.35 else (255, 170, 80, int(200 * (1 - t)))
            bd.ellipse([p[0] - r, p[1] - r, p[0] + r, p[1] + r], fill=col)
    burn = burn.filter(ImageFilter.GaussianBlur(S * 0.006))
    img = Image.alpha_composite(img, burn)
    # shadow, body, highlights
    sh = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(sh).polygon([(x + S * 0.012, y + S * 0.02) for x, y in map(tf, body)], fill=(0, 0, 0, 120))
    sh = sh.filter(ImageFilter.GaussianBlur(S * 0.01))
    img = Image.alpha_composite(img, sh)
    d = ImageDraw.Draw(img)
    d.polygon([tf(p) for p in body], fill=(78, 90, 108, 255))
    # lit upper half (sun from below right -> lighter starboard side)
    upper = [p for p in body if p[1] <= 0.001] + [(-0.70, 0.0)]
    d.polygon([tf(p) for p in upper], fill=(112, 126, 146, 255))
    d.polygon([tf(p) for p in spine], fill=(140, 154, 172, 255))
    d.polygon([tf(p) for p in canopy], fill=(214, 168, 64, 255))
    d.polygon([tf(p) for p in [(0.74, 0.0), (0.64, 0.02), (0.54, 0.022), (0.58, 0.0)]], fill=(255, 236, 170, 255))
    img = img.resize((1024, 1024), Image.LANCZOS)
    mask = squircle_mask(1024)
    out = Image.new("RGBA", (1024, 1024), (0, 0, 0, 0))
    out.paste(img, (0, 0), mask)
    out.save(out_png)
    out.save(out_icns, sizes=[(16, 16), (32, 32), (64, 64), (128, 128), (256, 256), (512, 512), (1024, 1024)])

main(sys.argv[1], sys.argv[2])
