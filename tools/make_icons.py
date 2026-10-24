# The launcher icon and the store icons, in the app's Poster look: a yellow screen, the black
# button rail on the right with three dots, and a bold check. Run: python3 tools/make_icons.py
from PIL import Image, ImageDraw

YELLOW = (255, 170, 0)  # GColorChromeYellow, the routine colour
BLACK = (0, 0, 0)
SS = 8  # drawn this many times larger, then scaled down


def icon(size, rail=True, radius=0):
    s = size * SS
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((0, 0, s - 1, s - 1), radius=radius * SS, fill=YELLOW)
    body = s
    if rail:
        rail_w = round(s * 0.2)
        body = s - rail_w
        d.rounded_rectangle((body, 0, s - 1, s - 1), radius=radius * SS, fill=BLACK)
        d.rectangle((body, 0, s - 1 - radius * SS, s - 1), fill=BLACK)
        r = s * 0.035
        for fy in (52 / 228, 114 / 228, 176 / 228):  # the watch's button centres
            cx, cy = body + rail_w / 2, s * fy
            d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=YELLOW)
    w = round(s * (0.11 if rail else 0.14))
    pts = [(body * 0.22, s * 0.52), (body * 0.43, s * 0.72), (body * 0.80, s * 0.30)]
    d.line(pts, fill=BLACK, width=w, joint="curve")
    for x, y in (pts[0], pts[-1]):  # round caps
        d.ellipse((x - w / 2, y - w / 2, x + w / 2, y + w / 2), fill=BLACK)
    return img.resize((size, size), Image.LANCZOS)


if __name__ == "__main__":
    icon(144).save("store/icon-large-144.png")
    icon(80).save("store/icon-small-80.png")
    icon(25, rail=False, radius=5).save("resources/images/menu_icon.png")
