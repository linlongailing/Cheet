from PIL import Image, ImageDraw
import math
import os

out = os.path.join(os.path.dirname(__file__), "..", "resources", "icons", "sites")
os.makedirs(out, exist_ok=True)


def save(name, draw_fn):
    img = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    draw_fn(d)
    path = os.path.join(out, f"{name}.png")
    img.save(path)
    print(path)


def circle_bg(d, color):
    d.ellipse((2, 2, 61, 61), fill=color)


def chatgpt(d):
    circle_bg(d, (16, 163, 127, 255))
    d.ellipse((18, 18, 46, 46), outline=(255, 255, 255, 255), width=4)
    d.arc((12, 12, 52, 52), 30, 210, fill=(255, 255, 255, 255), width=3)


def gemini(d):
    circle_bg(d, (30, 30, 40, 255))
    d.ellipse((10, 18, 34, 42), fill=(66, 133, 244, 255))
    d.ellipse((30, 14, 54, 38), fill=(234, 67, 53, 220))
    d.ellipse((20, 30, 48, 56), fill=(251, 188, 5, 220))
    d.ellipse((26, 22, 42, 38), fill=(52, 168, 83, 230))


def claude(d):
    circle_bg(d, (217, 119, 87, 255))
    cx, cy = 32, 32
    for a in range(0, 360, 45):
        rad = math.radians(a)
        r1, r2 = 10, 22
        x1, y1 = cx + r1 * math.cos(rad), cy + r1 * math.sin(rad)
        x2, y2 = cx + r2 * math.cos(rad), cy + r2 * math.sin(rad)
        d.line((x1, y1, x2, y2), fill=(255, 255, 255, 255), width=3)
    d.ellipse((24, 24, 40, 40), fill=(255, 255, 255, 255))


def doubao(d):
    circle_bg(d, (50, 120, 255, 255))
    d.ellipse((16, 20, 48, 48), fill=(255, 255, 255, 255))
    d.ellipse((24, 26, 34, 36), fill=(50, 120, 255, 255))
    d.ellipse((36, 26, 46, 36), fill=(50, 120, 255, 255))
    d.arc((22, 32, 42, 46), 20, 160, fill=(50, 120, 255, 255), width=3)


def qwen(d):
    circle_bg(d, (97, 80, 255, 255))
    d.ellipse((16, 14, 48, 46), outline=(255, 255, 255, 255), width=5)
    d.line((38, 40, 50, 52), fill=(255, 255, 255, 255), width=5)


def deepseek(d):
    circle_bg(d, (30, 64, 175, 255))
    d.ellipse((12, 22, 52, 48), fill=(147, 197, 253, 255))
    d.ellipse((38, 18, 54, 34), fill=(191, 219, 254, 255))
    d.ellipse((20, 30, 28, 38), fill=(30, 64, 175, 255))


for n, fn in [
    ("chatgpt", chatgpt),
    ("gemini", gemini),
    ("claude", claude),
    ("doubao", doubao),
    ("qwen", qwen),
    ("deepseek", deepseek),
]:
    save(n, fn)
print("done")
