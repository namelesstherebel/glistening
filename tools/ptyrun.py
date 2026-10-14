#!/usr/bin/env python3
"""Run glistening in a pseudo-terminal, send keys, and capture what a terminal
would show (pyte emulation). Kitty graphics APCs are stripped and checked.

usage: ptyrun.py COLSxROWS OUT.png [--kitty] [-- key-script]
key script: whitespace-separated steps, e.g. "wait:3 tab wait:1 down enter wait:2 ?"
Audio: $SONG (f32le stereo 44.1k); by default the render gate's fixture track,
written by `tests/render --song` into a temporary directory. Config goes there too
unless $XDG_CONFIG_HOME is set.
"""
import os, pty, re, select, shlex, struct, subprocess, sys, tempfile, time, fcntl, termios, base64
import pyte
from PIL import Image, ImageDraw, ImageFont

size, out = sys.argv[1], sys.argv[2]
cols, rows = map(int, size.split("x"))
kitty = "--kitty" in sys.argv
script = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else ["wait:4"]
work = tempfile.mkdtemp(prefix="glistening-pty-")
song = os.environ.get("SONG")
if not song:
    song = os.path.join(work, "song.f32")
    subprocess.run(["make", "-s", "tests/render"], check=True)  # not built by the default target
    subprocess.run(["./tests/render", "--song", song], check=True, stdout=subprocess.DEVNULL)
if not os.path.isfile(song):
    sys.exit(f"ptyrun: no audio fixture at {song}")
envv = dict(os.environ, TERM="xterm-256color", VIZ_OUTPUT="kitty" if kitty else "blocks", VIZ_MEDIUM="direct",
            XDG_CONFIG_HOME=os.environ.get("XDG_CONFIG_HOME", work),
            VIZ_PCM_CMD=f"exec ffmpeg -loglevel error -re -f f32le -ar 44100 -ac 2 -i {shlex.quote(song)} -f f32le -")
pid, fd = pty.fork()
if pid == 0:
    os.execve("./glistening", ["./glistening"] + os.environ.get("ARGS", "").split(), envv)
fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, cols * 10, rows * 20))
os.kill(pid, 28)  # SIGWINCH

screen = pyte.Screen(cols, rows)
stream = pyte.ByteStream(screen)
apc = re.compile(rb"\x1b_G([^;\x1b]*);?([^\x1b]*)\x1b\\")
stats = {"bytes": 0, "images": 0, "chunks": 0, "bad": 0}  # images: transmissions that reached their final (m=0) chunk
pending = b""
inflight = False  # an image transmission announced more chunks (m=1)
shm_tx = False
expect, got = 0, 0  # pixel bytes the open transmission declared (s*v*3 or *4) and decoded so far

def pump(secs):
    global pending, inflight, expect, got, shm_tx
    end = time.time() + secs
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if not r:
            continue
        try:
            data = os.read(fd, 1 << 20)
        except OSError:
            return False
        stats["bytes"] += len(data)
        data = pending + data
        cut = data.rfind(b"\x1b_G")  # keep an unfinished APC (or a lone trailing ESC) for the next read
        if cut >= 0 and data.find(b"\x1b\\", cut) < 0:
            data, pending = data[:cut], data[cut:]
        elif data.endswith(b"\x1b_"):  # an APC introducer split across reads
            data, pending = data[:-2], b"\x1b_"
        elif data.endswith(b"\x1b"):
            data, pending = data[:-1], b"\x1b"
        else:
            pending = b""
        for m in apc.finditer(data):
            keys = dict(kv.split(b"=") for kv in m.group(1).split(b",") if b"=" in kv)
            if keys.get(b"a") in (b"T", b"t"):
                if inflight:  # the previous transmission never sent its final chunk
                    stats["bad"] += 1
                inflight = True
                bpp = 3 if keys.get(b"f", b"32") == b"24" else 4
                w, h = int(keys.get(b"s", b"0")), int(keys.get(b"v", b"0"))
                # direct: the payload is the pixels; shm: S= is the object's size
                expect, got = w * h * bpp, (int(keys.get(b"S", b"-1")) if keys.get(b"t") == b"s" else 0)
                shm_tx = keys.get(b"t") == b"s"
            if m.group(2):
                stats["chunks"] += 1
                try:
                    raw = base64.b64decode(m.group(2), validate=True)
                    if inflight and not shm_tx:
                        got += len(raw)
                except Exception:
                    stats["bad"] += 1
            if inflight and keys.get(b"m", b"0") == b"0":
                if got != expect:  # truncated or padded pixel data
                    stats["bad"] += 1
                stats["images"] += 1
                inflight = False
        stream.feed(apc.sub(b"", data))
    return True

pump(1.0)
for step in script:
    if step.startswith("wait:"):
        pump(float(step[5:]))
        continue
    if step.startswith("resize:"):  # resize:COLSxROWS mid-run
        cols, rows = map(int, step[7:].split("x"))
        fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, cols * 10, rows * 20))
        screen.resize(rows, cols)
        os.kill(pid, 28)
        pump(1.0)
        continue
    key = {"tab": "\t", "enter": "\r", "esc": "\x1b", "up": "\x1b[A", "down": "\x1b[B", "left": "\x1b[D", "right": "\x1b[C",
           "space": " "}.get(step, step)
    os.write(fd, key.encode())
    pump(0.4)

# Render the emulated screen: cells with ▀ become two colour rectangles.
cw, ch = 10, 20
img = Image.new("RGB", (cols * cw, rows * ch), (0, 0, 0))
d = ImageDraw.Draw(img)
font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 14)
bold = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", 14)
def rgb(c, default):
    if c in ("default", None) or not re.fullmatch(r"[0-9a-fA-F]{6}", str(c)):
        return default
    return tuple(int(c[i:i + 2], 16) for i in (0, 2, 4))
for y in range(rows):
    for x in range(cols):
        c = screen.buffer[y][x]
        fg, bg = rgb(c.fg, (220, 220, 220)), rgb(c.bg, (0, 0, 0))
        if c.data == "▀":
            d.rectangle([x * cw, y * ch, x * cw + cw - 1, y * ch + ch // 2 - 1], fill=fg)
            d.rectangle([x * cw, y * ch + ch // 2, x * cw + cw - 1, y * ch + ch - 1], fill=bg)
        else:
            d.rectangle([x * cw, y * ch, x * cw + cw - 1, y * ch + ch - 1], fill=bg)
            if c.data.strip():
                d.text((x * cw + cw / 2, y * ch + ch / 2), c.data, fill=fg, font=bold if c.bold else font, anchor="mm")
img.save(out)
os.write(fd, b"q")
status = None
for _ in range(50):  # keep draining output while it shuts down
    pump(0.1)
    p, st = os.waitpid(pid, os.WNOHANG)
    if p:
        status = st
        break
if status is None:
    os.kill(pid, 9)
    os.waitpid(pid, 0)
    status = 255 << 8
text = "\n".join(screen.display)
print(f"exit={status >> 8} bytes={stats['bytes']} images={stats['images']} chunks={stats['chunks']} bad_b64={stats['bad']}")
print(text if os.environ.get("SHOW") else "")
if not os.WIFEXITED(status) or os.WEXITSTATUS(status) != 0:  # crashed, killed, or exited with an error
    sys.exit(f"ptyrun: glistening did not exit cleanly (status {status:#x})")
if stats["bad"]:
    sys.exit(f"ptyrun: {stats['bad']} malformed kitty payloads")
if kitty and (not stats["images"] or not stats["chunks"]):
    sys.exit("ptyrun: --kitty run produced no complete images")
if inflight:
    sys.exit("ptyrun: an image transmission never sent its final (m=0) chunk")
if b"\x1b_G" in pending:
    sys.exit("ptyrun: output ended inside an unterminated kitty APC")
