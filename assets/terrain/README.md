# Real terrain data

This folder holds the elevation files for SKYFALL's WORLD LOCATIONS (`<id>.skt`, ~2 MB each).
They are generated, not hand-made:

* **GitHub:** Actions → *real terrain* → *Run workflow* (downloads and commits all locations), or
* **locally:** `python3 tools/fetch_real_terrain.py` (Python 3 standard library only).

Source: AWS Terrain Tiles (Mapzen "terrarium" encoding; SRTM, GMTED2010, ETOPO1, NED and other
open datasets — attribution: https://github.com/tilezen/joerd/blob/master/docs/attribution.md).
