#!/usr/bin/env python3
"""Record SKYFALL's voice pack: every briefing and radio line, spoken by a neural female voice.

Voice model: Kokoro-82M (Apache-2.0) via kokoro-onnx; model files from
https://github.com/thewh1teagle/kokoro-onnx/releases (model-files-v1.0).
Lines come from the game itself (`skyfall_voicelines`), so the recordings always match the text.
Radio calls get a radio-band filter; the briefing officer is clean studio audio.

    pip install kokoro-onnx soundfile
    cmake --build build --target skyfall_voicelines
    python3 tools/make_voice.py --model-dir /path/to/kokoro      # writes assets/voice/*.mp3

Needs ffmpeg (MP3 encoding). Existing recordings are kept; stale ones are removed.
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model-dir", required=True)
    ap.add_argument("--lines", default=os.path.join(ROOT, "build", "skyfall_voicelines"))
    ap.add_argument("--out", default=os.path.join(ROOT, "assets", "voice"))
    ap.add_argument("--speed", type=float, default=1.0)
    args = ap.parse_args()

    import soundfile as sf
    from kokoro_onnx import Kokoro

    lines = [json.loads(l) for l in subprocess.check_output([args.lines], text=True).splitlines() if l.strip()]
    os.makedirs(args.out, exist_ok=True)
    wanted = {l["key"] for l in lines}
    for f in os.listdir(args.out):
        if f.endswith(".mp3") and f[:-4] not in wanted:
            os.remove(os.path.join(args.out, f))
            print("removed stale", f)
    todo = [l for l in lines if not os.path.exists(os.path.join(args.out, l["key"] + ".mp3"))]
    print("%d lines, %d to record" % (len(lines), len(todo)))
    if not todo:
        return
    k = Kokoro(os.path.join(args.model_dir, "kokoro-v1.0.onnx"), os.path.join(args.model_dir, "voices-v1.0.bin"))
    with tempfile.TemporaryDirectory() as tmp:
        for i, l in enumerate(todo):
            lang = "en-gb" if l["voice"].startswith("b") else "en-us"
            # radio calls are a touch quicker and clipped, the briefing is measured
            speed = args.speed * (1.06 if l["radio"] else 0.98)
            samples, sr = k.create(l["text"], voice=l["voice"], speed=speed, lang=lang)
            wav = os.path.join(tmp, "l.wav")
            sf.write(wav, samples, sr)
            if l["radio"]:
                # VHF/UHF radio: band-limited, slightly compressed and driven, quiet hiss
                af = ("highpass=f=320,lowpass=f=3300,acompressor=threshold=-18dB:ratio=4:attack=5:release=60,"
                      "volume=1.6,alimiter=limit=0.9")
            else:
                af = "highpass=f=70,acompressor=threshold=-20dB:ratio=2.5:attack=10:release=120,loudnorm=I=-16:TP=-1.5"
            out = os.path.join(args.out, l["key"] + ".mp3")
            subprocess.check_call(["ffmpeg", "-loglevel", "error", "-y", "-i", wav, "-af", af, "-ac", "1", "-ar", "24000",
                                   "-codec:a", "libmp3lame", "-q:a", "5", out])
            sys.stdout.write("\r  recorded %d/%d" % (i + 1, len(todo)))
            sys.stdout.flush()
    print()
    with open(os.path.join(args.out, "ATTRIBUTION.md"), "w") as f:
        f.write("# Voice\n\nAll spoken lines were synthesised with Kokoro-82M (Apache-2.0, hexgrad/Kokoro-82M) via\n"
                "kokoro-onnx (MIT). Voices: bf_isabella (briefings, AWACS, instructor), bf_emma (tower), af_bella (wingman).\n"
                "Regenerate with `tools/make_voice.py`.\n")


if __name__ == "__main__":
    main()
