#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# Regenerates the binary fixtures in data/. Needs Pillow, numpy, ImageMagick (convert) and ffmpeg
# with libvorbis. The generated files are committed, so this is only needed to change them.
import subprocess, os, numpy as np
from PIL import Image

d = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")
os.makedirs(d, exist_ok=True)

# 8x8 RGBA with every channel distinct per pixel so any channel swap/flip is detected.
px = np.zeros((8, 8, 4), np.uint8)
for y in range(8):
    for x in range(8):
        px[y, x] = (x * 32, y * 32, (x + y) * 16, 255 - x * 8 - y * 8)
Image.fromarray(px, "RGBA").save(f"{d}/rgba_8x8.png")
Image.fromarray(px[:, :, :3], "RGB").save(f"{d}/rgb_8x8.png")

# Grayscale and palette.
gray = np.arange(64, dtype=np.uint8).reshape(8, 8) * 4
Image.fromarray(gray, "L").save(f"{d}/gray_8x8.png")
pal = Image.new("P", (4, 4))
pal.putpalette([255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 0] + [0] * 756)
for y in range(4):
    for x in range(4):
        pal.putpixel((x, y), (x + y) % 4)
pal.save(f"{d}/palette_4x4.png")

# 16-bit grayscale PNG (stb reduces to 8 bit by taking the high byte).
g16 = (np.arange(64, dtype=np.uint16).reshape(8, 8) * 1000)
Image.fromarray(g16, "I;16").save(f"{d}/gray16_8x8.png")

# Interlaced (Adam7) copy of the RGBA image.
subprocess.run(["convert", f"{d}/rgba_8x8.png", "-interlace", "PNG", f"PNG32:{d}/rgba_8x8_interlaced.png"], check=True)

# JPEG: smooth gradient (so lossy error stays small), baseline and progressive.
g = np.zeros((32, 32, 3), np.uint8)
for y in range(32):
    for x in range(32):
        g[y, x] = (x * 8, y * 8, 128)
img = Image.fromarray(g, "RGB")
img.save(f"{d}/gradient_32.jpg", quality=95, subsampling=0)
img.save(f"{d}/gradient_32_progressive.jpg", quality=95, subsampling=0, progressive=True)
Image.new("RGB", (16, 16), (200, 30, 60)).save(f"{d}/solid_16.jpg", quality=95, subsampling=0)

# Ogg Vorbis: 0.5 s, 44.1 kHz. Mono 440 Hz; stereo 440 Hz left / 880 Hz right.
def tone(freq, n=22050, sr=44100):
    return np.sin(2 * np.pi * freq * np.arange(n) / sr) * 0.5
def write_wav(path, chans, sr=44100):
    import wave
    data = np.stack(chans, axis=1)
    pcm = (data * 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(len(chans)); w.setsampwidth(2); w.setframerate(sr); w.writeframes(pcm.tobytes())
tmp = f"{d}/_tmp.wav"
write_wav(tmp, [tone(440)])
subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", tmp, "-c:a", "libvorbis", "-q:a", "6", f"{d}/sine440_mono.ogg"], check=True)
write_wav(tmp, [tone(440), tone(880)])
subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", tmp, "-c:a", "libvorbis", "-q:a", "6", f"{d}/stereo_440_880.ogg"], check=True)
os.remove(tmp)
print("ok")
