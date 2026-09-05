# -*- coding: utf-8 -*-
"""生成多尺寸标准 ICO 图标 app.ico"""
from PIL import Image, ImageDraw
import math

try:
    import numpy as np
    HAVE_NP = True
except Exception:
    HAVE_NP = False


def make(size):
    if HAVE_NP:
        yy, xx = np.mgrid[0:size, 0:size]
        # 与运行时 app_icon 完全一致：对角线性渐变 (0,0)->(px,px) #52B7FF -> #136FF0
        t = (xx + yy) / (2.0 * (size - 1))
        c1 = np.array([0x52, 0xB7, 0xFF], dtype=np.float64)
        c2 = np.array([0x13, 0x6F, 0xF0], dtype=np.float64)
        rgb = c1[None, None, :] + (c2 - c1)[None, None, :] * t[..., None]
        a = np.where((xx - (size - 1) / 2.0) ** 2 + (yy - (size - 1) / 2.0) ** 2
                     <= (size / 2.0 - 0.5) ** 2, 255, 0).astype(np.uint8)
        img = Image.fromarray(np.dstack([rgb.astype(np.uint8), a]), "RGBA")
    else:
        img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    m = size / 2.0
    # 内圈（与 app_icon 一致：0.12 偏移、0.76 尺寸、alpha 26）
    d.ellipse([m - size * 0.38, m - size * 0.38, m + size * 0.38, m + size * 0.38],
              fill=(255, 255, 255, 26))
    lw = max(1, int(round(size * 0.09)))
    # 时针向上：长 0.30*size；分针斜向：长 0.75*0.30*size，角度 pi/2.7
    d.line([m, m, m, m - size * 0.30], fill=(255, 255, 255, 255), width=lw)
    a2 = math.pi / 2.7
    d.line([m, m,
            m + size * 0.30 * 0.75 * math.cos(a2),
            m - size * 0.30 * 0.75 * math.sin(a2)],
           fill=(255, 255, 255, 255), width=lw)
    return img


sizes = [(256, 256), (128, 128), (64, 64), (48, 48), (32, 32), (24, 24), (16, 16)]
img = make(256)
img.save("app.ico", format="ICO", sizes=sizes)
print("saved app.ico frames:", [s[0] for s in sizes])