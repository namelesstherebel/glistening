#!/usr/bin/env python3
"""Capture deterministic demo videos from the glistening binary."""
import argparse, hashlib, json, math, os, platform, random, shutil, struct
import subprocess, sys, tempfile, wave
from pathlib import Path

IDS = "aurora mercury galaxy tunnel ridgeline ink quasicrystal phosphor rainglass cathedral magnetic loom papercity clockwork".split()
R, SECS = 44100, 6
FPS, SEED, W, H = 24, 7, 640, 360
FRAMES = FPS * SECS


def version(cmd):
    try:
        return run(cmd, capture_output=True, text=True).stdout.splitlines()[0]
    except Exception:
        return "unknown"


def run(cmd, **kw):
    return subprocess.run(cmd, check=True, shell=False, **kw)


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def synth(path):
    rnd = random.Random(SEED)
    notes = [220.0, 261.63, 329.63, 392.0]
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(R)
        buf = bytearray()
        for i in range(R * SECS):
            t = i / R
            beat = t % 0.5
            s = 0.3 * math.sin(2 * math.pi * 55 * t)
            s += 0.15 * math.sin(2 * math.pi * notes[int(t * 8) % 4] * t)
            s += 0.5 * math.sin(2 * math.pi * (50 + 80 * math.exp(-beat * 30)) * beat) * math.exp(-beat * 8)
            h = t % 0.25
            n = rnd.uniform(-1, 1) * 0.15 * math.exp(-h * 40)
            l, r = s + n, s - n * 0.5
            for v in (l, r):
                buf += struct.pack("<h", int(max(-1, min(1, v)) * 32767))
        w.writeframes(bytes(buf))


def probe(p):
    o = run(["ffprobe", "-v", "error", "-show_entries",
             "stream=codec_type,codec_name,width,height,pix_fmt,avg_frame_rate,r_frame_rate,nb_frames"
             ":format=duration", "-of", "json", str(p)], capture_output=True, text=True).stdout
    d = json.loads(o)
    assert len(d["streams"]) == 2, "expected two streams"
    by = {s["codec_type"]: s for s in d["streams"]}
    v, au = by["video"], by["audio"]
    assert v["codec_name"] == "h264", "video codec must be h264"
    assert au["codec_name"] == "aac", "audio codec must be aac"
    assert (v["width"], v["height"]) == (W, H), "bad frame size"
    assert v["pix_fmt"] == "yuv420p", "bad pixel format"
    assert v["avg_frame_rate"] == f"{FPS}/1", "bad frame rate"
    assert int(v["nb_frames"]) == FRAMES, "bad frame count"
    assert abs(float(d["format"]["duration"]) - SECS) < 0.25, "bad duration"
    return d


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", default="build/glistening")
    ap.add_argument("--output", default="docs/demos")
    ap.add_argument("--scene", choices=IDS)
    ap.add_argument("--force", action="store_true")
    a = ap.parse_args()
    for t in ("ffmpeg", "ffprobe"):
        if not shutil.which(t):
            sys.exit(f"{t} required on PATH")
    binary, out = Path(a.binary), Path(a.output)
    if not binary.is_file():
        sys.exit(f"binary not found: {binary}")
    scenes = [a.scene] if a.scene else IDS
    out.mkdir(parents=True, exist_ok=True)
    targets = [out / "demo-audio.wav"] + [out / f"{s}.{e}" for s in scenes for e in ("mp4", "jpg")]
    if not a.force:
        ex = [str(t) for t in targets if t.exists()]
        if ex:
            sys.exit("refusing to overwrite (use --force): " + ", ".join(ex))
    try:
        sw = run(["git", "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    except Exception:
        sw = "unknown"
    assets, cmds = {}, []
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        wav = tmp / "audio.wav"
        synth(wav)
        shutil.copyfile(wav, out / "demo-audio.wav")
        assets["demo-audio.wav"] = {"sha256": sha(wav), "bytes": wav.stat().st_size}
        cfg = tmp / "config"; cfg.mkdir()
        env = dict(os.environ, VIZ_SEED=str(SEED), VIZ_FPS=str(FPS), XDG_CONFIG_HOME=str(cfg), APPDATA=str(cfg))
        for s in scenes:
            fd = tmp / s; fd.mkdir()
            c = [str(binary), "--file", str(wav), "--mute", "--no-director", "--scene", s,
                 "--dump", str(fd / "%05d.ppm"), "--seconds", str(SECS), "--size", f"{W}x{H}"]
            run(c, env=env)
            n = len(list(fd.glob("*.ppm")))
            assert n == FRAMES, f"{s}: expected {FRAMES} frames, got {n}"
            mp4, jpg = out / f"{s}.mp4", out / f"{s}.jpg"
            e1 = ["ffmpeg", "-y", "-v", "error", "-framerate", str(FPS), "-start_number", "0",
                  "-i", str(fd / "%05d.ppm"), "-i", str(wav), "-c:v", "libx264", "-crf", "28",
                  "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "96k",
                  "-movflags", "+faststart", "-t", str(SECS), str(mp4)]
            e2 = ["ffmpeg", "-y", "-v", "error", "-i", str(mp4), "-ss", "2", "-frames:v", "1", str(jpg)]
            run(e1); run(e2)
            info = probe(mp4)
            for f in (mp4, jpg):
                assets[f.name] = {"sha256": sha(f), "bytes": f.stat().st_size}
            assets[mp4.name]["probe"] = info
            for x in (c, e1, e2):
                cmds.append([p.replace(str(tmp), "<TEMP>").replace(str(Path.cwd()), ".") for p in x])
            shutil.rmtree(fd)
    man = {"source_git_sha": sw, "binary_sha256": sha(binary), "platform": platform.platform(),
           "tool_versions": {"ffmpeg": version(["ffmpeg", "-version"]),
                             "ffprobe": version(["ffprobe", "-version"]),
                             "python": sys.version.replace("\n", " ")},
           "capture": {"fps": FPS, "seed": SEED, "duration_seconds": SECS,
                       "size": f"{W}x{H}", "frames": FRAMES},
           "commands": cmds,
           "audio_provenance": "Original audio synthesized by this script (stdlib wave/math/random seed 7); no external audio.",
           "assets": assets}
    (out / "manifest.json").write_text(json.dumps(man, indent=2, sort_keys=True) + "\n")


if __name__ == "__main__":
    main()
