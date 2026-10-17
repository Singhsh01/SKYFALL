#!/usr/bin/env python3
"""Download real elevation data for SKYFALL's real-world locations.

Source: AWS Terrain Tiles ("terrarium" encoding, open data hosted by AWS; derived from SRTM,
GMTED2010, ETOPO1, NED and other public/open datasets - see
https://github.com/tilezen/joerd/blob/master/docs/attribution.md).

For every location a 32 x 32 km square centred on its coordinates is resampled to a
1025 x 1025 grid of whole metres and written to assets/terrain/<id>.skt (about 2 MB each),
which the game loads when you pick that location.

Usage:
    python3 tools/fetch_real_terrain.py                 # all locations
    python3 tools/fetch_real_terrain.py --only everest,rio
    python3 tools/fetch_real_terrain.py --zoom 13       # finer source tiles (slower)

Only the Python standard library is used (no pip installs needed).
"""
import argparse
import math
import os
import struct
import sys
import time
import urllib.request
import zlib

# Keep in sync with src/sim/Locations.cpp (id, latitude, longitude, name)
LOCATIONS = [
    ("everest", 27.95, 86.87, "Everest & Khumbu, Nepal"),
    ("matterhorn", 45.99, 7.72, "Matterhorn & Zermatt, Switzerland"),
    ("geiranger", 62.10, 7.15, "Geirangerfjord, Norway"),
    ("glencoe", 56.66, -5.05, "Glen Coe, Scotland"),
    ("erg_chebbi", 31.13, -4.00, "Erg Chebbi, Morocco (Sahara)"),
    ("wadi_rum", 29.57, 35.42, "Wadi Rum, Jordan"),
    ("musandam", 26.20, 56.25, "Musandam fjords, Oman"),
    ("grand_canyon", 36.10, -112.11, "Grand Canyon, USA"),
    ("aconcagua", -32.65, -70.01, "Aconcagua, Argentina"),
    ("torres_paine", -50.97, -73.00, "Torres del Paine, Chile"),
    ("rio", -22.95, -43.21, "Rio de Janeiro, Brazil"),
    ("guilin", 24.78, 110.45, "Guilin karst, China"),
    ("fuji", 35.36, 138.73, "Mount Fuji, Japan"),
]

TILE_URL = "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{z}/{x}/{y}.png"
EXTENT_M = 32000.0
N = 1025
EARTH_R = 6378137.0


# ---------------------------------------------------------------------------------------------
# Minimal PNG decoder (8-bit RGB / RGBA, non-interlaced) - enough for terrarium tiles
# ---------------------------------------------------------------------------------------------
def decode_png(data):
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos = 8
    width = height = None
    bit_depth = color_type = interlace = None
    idat = []
    while pos < len(data):
        length, ctype = struct.unpack(">I4s", data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if ctype == b"IHDR":
            width, height, bit_depth, color_type, _, _, interlace = struct.unpack(">IIBBBBB", chunk)
        elif ctype == b"IDAT":
            idat.append(chunk)
        elif ctype == b"IEND":
            break
    if bit_depth != 8 or color_type not in (2, 6) or interlace != 0:
        raise ValueError("unsupported PNG (depth %s, colour type %s)" % (bit_depth, color_type))
    bpp = 3 if color_type == 2 else 4
    raw = zlib.decompress(b"".join(idat))
    stride = width * bpp
    out = bytearray(height * stride)
    prev = bytearray(stride)
    i = 0
    for y in range(height):
        f = raw[i]
        line = bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        if f == 1:
            for x in range(bpp, stride):
                line[x] = (line[x] + line[x - bpp]) & 255
        elif f == 2:
            for x in range(stride):
                line[x] = (line[x] + prev[x]) & 255
        elif f == 3:
            for x in range(stride):
                left = line[x - bpp] if x >= bpp else 0
                line[x] = (line[x] + ((left + prev[x]) >> 1)) & 255
        elif f == 4:
            for x in range(stride):
                a = line[x - bpp] if x >= bpp else 0
                b = prev[x]
                c = prev[x - bpp] if x >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return width, height, bpp, out


def tile_elevations(png_bytes):
    """Terrarium: elevation = R*256 + G + B/256 - 32768 (metres)."""
    w, h, bpp, px = decode_png(png_bytes)
    elev = [0.0] * (w * h)
    for k in range(w * h):
        o = k * bpp
        elev[k] = px[o] * 256.0 + px[o + 1] + px[o + 2] / 256.0 - 32768.0
    return w, h, elev


# ---------------------------------------------------------------------------------------------
def mercator_px(lat, lon, zoom):
    n = (2 ** zoom) * 256.0
    x = (lon + 180.0) / 360.0 * n
    lr = math.radians(lat)
    y = (1.0 - math.asinh(math.tan(lr)) / math.pi) / 2.0 * n
    return x, y


def fetch(url, cache_dir, retries=4):
    key = url.split("terrarium/")[-1].replace("/", "_") if "terrarium/" in url else url.replace("/", "_")
    path = os.path.join(cache_dir, key)
    if os.path.exists(path) and os.path.getsize(path) > 0:
        with open(path, "rb") as f:
            return f.read()
    if url.startswith("file://"):
        with open(url[7:], "rb") as f:
            data = f.read()
    else:
        last = None
        for attempt in range(retries):
            try:
                req = urllib.request.Request(url, headers={"User-Agent": "SKYFALL-terrain-fetch/1.0"})
                with urllib.request.urlopen(req, timeout=60) as r:
                    data = r.read()
                break
            except Exception as e:  # network hiccup: back off and retry
                last = e
                time.sleep(1.5 * (attempt + 1))
        else:
            raise RuntimeError("download failed: %s (%s)" % (url, last))
    with open(path, "wb") as f:
        f.write(data)
    return data


def build_location(loc_id, lat0, lon0, zoom, tile_url, cache_dir):
    # local metric frame: x east, z south (matching the game), centred on (lat0, lon0)
    def latlon(x, z):
        lat = lat0 - math.degrees(z / EARTH_R)
        lon = lon0 + math.degrees(x / (EARTH_R * math.cos(math.radians(lat0))))
        return lat, lon

    half = EXTENT_M / 2.0
    corners = [mercator_px(*latlon(x, z), zoom) for x in (-half, half) for z in (-half, half)]
    px0 = min(c[0] for c in corners) - 2
    px1 = max(c[0] for c in corners) + 2
    py0 = min(c[1] for c in corners) - 2
    py1 = max(c[1] for c in corners) + 2
    tx0, tx1 = int(px0 // 256), int(px1 // 256)
    ty0, ty1 = int(py0 // 256), int(py1 // 256)
    tiles = {}
    total = (tx1 - tx0 + 1) * (ty1 - ty0 + 1)
    done = 0
    for ty in range(ty0, ty1 + 1):
        for tx in range(tx0, tx1 + 1):
            url = tile_url.format(z=zoom, x=tx, y=ty)
            tiles[(tx, ty)] = tile_elevations(fetch(url, cache_dir))
            done += 1
            sys.stdout.write("\r  %s: tiles %d/%d" % (loc_id, done, total))
            sys.stdout.flush()
    sys.stdout.write("\n")

    def elev_px(px, py):
        tx, ty = int(px // 256), int(py // 256)
        w, h, e = tiles[(tx, ty)]
        lx = min(w - 1, max(0, int(px - tx * 256)))
        ly = min(h - 1, max(0, int(py - ty * 256)))
        return e[ly * w + lx]

    def sample(px, py):
        x0, y0 = math.floor(px - 0.5), math.floor(py - 0.5)
        fx, fy = px - 0.5 - x0, py - 0.5 - y0
        a = elev_px(x0, y0)
        b = elev_px(x0 + 1, y0)
        c = elev_px(x0, y0 + 1)
        d = elev_px(x0 + 1, y0 + 1)
        return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy

    heights = []
    step = EXTENT_M / (N - 1)
    for j in range(N):
        z = -half + j * step
        for i in range(N):
            x = -half + i * step
            lat, lon = latlon(x, z)
            px, py = mercator_px(lat, lon, zoom)
            heights.append(sample(px, py))
    return heights


def write_skt(path, heights, lat, lon):
    mn, mx = min(heights), max(heights)
    with open(path, "wb") as f:
        f.write(b"SKT1")
        f.write(struct.pack("<ifffffi", N, EXTENT_M, mn, mx, lat, lon, 0))
        f.write(struct.pack("<%dh" % len(heights), *[int(max(-32000, min(32000, round(h)))) for h in heights]))
    return mn, mx


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--only", help="comma-separated location ids")
    ap.add_argument("--zoom", type=int, default=12, help="source tile zoom (12 = ~30 m, 13 = ~15 m)")
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "terrain"))
    ap.add_argument("--tile-url", default=TILE_URL, help="tile URL template ({z}/{x}/{y}); file:// allowed")
    ap.add_argument("--cache", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".terrain_cache"))
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    os.makedirs(args.cache, exist_ok=True)
    wanted = set(args.only.split(",")) if args.only else None
    failures = 0
    for loc_id, lat, lon, name in LOCATIONS:
        if wanted and loc_id not in wanted:
            continue
        print("%s (%s)" % (name, loc_id))
        try:
            h = build_location(loc_id, lat, lon, args.zoom, args.tile_url, args.cache)
            mn, mx = write_skt(os.path.join(args.out, loc_id + ".skt"), h, lat, lon)
            print("  wrote %s.skt  elevation %.0f .. %.0f m" % (loc_id, mn, mx))
        except Exception as e:
            failures += 1
            print("  FAILED: %s" % e)
    with open(os.path.join(args.out, "ATTRIBUTION.md"), "w") as f:
        f.write("# Real terrain data\n\n"
                "Generated by `tools/fetch_real_terrain.py` from AWS Terrain Tiles (Mapzen terrarium encoding),\n"
                "open data derived from SRTM, GMTED2010, ETOPO1, NED, Copernicus and other sources.\n"
                "Attribution: https://github.com/tilezen/joerd/blob/master/docs/attribution.md\n\n"
                "Each `.skt` file: 32 x 32 km, 1025 x 1025 samples, int16 metres (format in src/sim/Locations.hpp).\n")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
