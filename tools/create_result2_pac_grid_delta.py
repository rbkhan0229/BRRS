from PIL import Image, ImageDraw, ImageFont
from pathlib import Path


OUT_DIR = Path(__file__).resolve().parents[2] / "reports"
OUT = OUT_DIR / "result2_pac_grid_delta_timeline.png"
FONT = "/System/Library/Fonts/AppleSDGothicNeo.ttc"


def font(size, index=0):
    return ImageFont.truetype(FONT, size=size, index=index)


S = 2
W, H = 1700, 1320
img = Image.new("RGB", (W * S, H * S), "#0f1011")
draw = ImageDraw.Draw(img)


def sc(v):
    return int(round(v * S))


def xy(x, y):
    return (sc(x), sc(y))


def rect(x1, y1, x2, y2, fill, radius=0, outline=None, width=1):
    box = (sc(x1), sc(y1), sc(x2), sc(y2))
    if radius:
        draw.rounded_rectangle(box, radius=sc(radius), fill=fill, outline=outline, width=sc(width))
    else:
        draw.rectangle(box, fill=fill, outline=outline, width=sc(width) if outline else 1)


def line(x1, y1, x2, y2, fill, width=1, dash=None):
    if dash is None:
        draw.line((sc(x1), sc(y1), sc(x2), sc(y2)), fill=fill, width=sc(width))
        return
    # vertical/horizontal dashed lines only
    if abs(x1 - x2) < 1e-6:
        y = y1
        on, off = dash
        while y < y2:
            draw.line((sc(x1), sc(y), sc(x2), sc(min(y + on, y2))), fill=fill, width=sc(width))
            y += on + off
    elif abs(y1 - y2) < 1e-6:
        x = x1
        on, off = dash
        while x < x2:
            draw.line((sc(x), sc(y1), sc(min(x + on, x2)), sc(y2)), fill=fill, width=sc(width))
            x += on + off


def text(x, y, s, fill="#e7e2d4", size=22, weight=0, anchor=None):
    draw.text(xy(x, y), s, fill=fill, font=font(size, weight), anchor=anchor)


def centered_text(x1, y1, x2, y2, s, fill="#202933", size=20, weight=0):
    f = font(size, weight)
    bbox = draw.textbbox((0, 0), s, font=f)
    tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    x = sc((x1 + x2) / 2) - tw // 2
    y = sc((y1 + y2) / 2) - th // 2 - sc(1)
    draw.text((x, y), s, fill=fill, font=f)


COL = {
    "ink": "#e7e2d4",
    "muted": "#aaa392",
    "axis": "#8e8a7f",
    "preamble": "#82b7e8",
    "sfd": "#2f84d6",
    "delta": "#ee8d9a",
    "discard": "#928d8d",
    "detect": "#fac76d",
    "accum": "#bfe095",
    "red": "#ff5b5b",
    "orange": "#ffb33e",
    "green": "#65a832",
}

x0 = 430
px = 23
sym_us = 1.0177
code_early_minus_actual_us = 42 - (32 * sym_us + 8 * sym_us)
delta_us = 11.3
delta_sym = delta_us / sym_us


def X(sym):
    return x0 + px * sym


def cmd_sym(lead):
    return -((lead + code_early_minus_actual_us) / sym_us)


def listen_sym(lead):
    return cmd_sym(lead) + delta_sym


def draw_bar(x1, x2, y, h, color, label=None, fill_text="#202933", size=19, radius=5):
    rect(x1, y, x2, y + h, color, radius=radius)
    if label:
        centered_text(x1, y, x2, y + h, label, fill=fill_text, size=size, weight=0)


# Header
text(82, 48, "Timeline + PAC grid with δ", size=38, weight=0)
text(82, 88, "result2 only · 32sym · 1m office · t=0 = 실제 preamble 시작", fill=COL["muted"], size=22)
text(
    82,
    118,
    f"현재 코드: code preamble+SFD=42us, actual=40.7us → RX_ON command는 lead+{code_early_minus_actual_us:.1f}us만큼 전단에 예약",
    fill=COL["muted"],
    size=20,
)
text(82, 147, "도식용 δ≈11.3us: lead=10us에서 listen start가 t≈0에 정렬되도록 잡은 값", fill=COL["muted"], size=20)

# Legend
legend_y = 184
items = [
    ("discarded PAC", COL["discard"]),
    ("δ: RX_ON command → 실제 listen start", COL["delta"]),
    ("detection ≈20sym", COL["detect"]),
    ("accumulation", COL["accum"]),
]
lx = 82
for label, color in items:
    rect(lx, legend_y, lx + 32, legend_y + 22, color, radius=2)
    text(lx + 45, legend_y - 1, label, size=18)
    lx += 330 if "δ:" in label else 230
text(1375, legend_y, "▼ listen start", fill="#b7b1a4", size=18)

# Packet and grid
line(X(-8), 275, X(40), 275, COL["axis"], width=2)
for s in [-16, -8, 0, 8, 16, 24, 32, 40]:
    line(X(s), 278, X(s), 1110, "#d6d0bd", width=1.5)
for s in [-8, 0, 8, 16, 24, 32, 40]:
    line(X(s), 263, X(s), 287, COL["axis"], width=2)
    text(X(s) - 10, 305, str(s), fill=COL["muted"], size=15)
line(X(0), 210, X(0), 1115, "#9b9588", width=2, dash=(7, 8))
line(X(32), 210, X(32), 1115, "#9b9588", width=2, dash=(7, 8))
text(X(0) + 10, 226, "t=0", fill=COL["muted"], size=17)
text(X(30), 226, "preamble end", fill=COL["muted"], size=17)

draw_bar(X(0), X(32), 220, 42, COL["preamble"], "preamble 32sym (32.56us)", fill_text=COL["ink"], size=22)
draw_bar(X(32), X(40), 220, 42, COL["sfd"], "SFD", fill_text="#ffffff", size=22)


def row(lead, y, per, detail, accum, color, special=None):
    ls = listen_sym(lead)
    cs = cmd_sym(lead)
    text(82, y + 38, f"lead {lead}", size=25)
    marker = "aligned (t=0)" if abs(ls) < 0.55 else (f"{abs(round(ls))}sym early" if ls < 0 else f"+{round(ls)}sym late")
    text(X(ls) - 58, y + 10, f"▼ {marker}", fill="#b7b1a4", size=19)
    draw_bar(X(cs), X(ls), y + 56, 38, COL["delta"], "δ≈11.3us", fill_text=COL["ink"], size=16)

    if special == "lead14":
        draw_bar(X(ls), X(ls + 20), y + 52, 33, COL["detect"], None)
        draw_bar(X(ls + 20), X(32), y + 52, 33, COL["accum"], "PAC 인정 → accum 16", size=16)
        draw_bar(X(ls), X(ls + 8), y + 96, 33, COL["discard"], None)
        draw_bar(X(ls + 8), X(ls + 28), y + 96, 33, COL["detect"], None)
        draw_bar(X(ls + 28), X(32), y + 96, 33, COL["accum"], "accum 8", size=16)
    elif special == "lead15":
        draw_bar(X(ls), X(ls + 8), y + 56, 38, COL["discard"], "discarded PAC", fill_text=COL["muted"], size=14)
        draw_bar(X(ls + 8), X(ls + 28), y + 56, 38, COL["detect"], "detection ≈20sym", size=18)
        draw_bar(X(ls + 28), X(32), y + 56, 38, COL["accum"], f"accum {accum}", size=18)
    else:
        draw_bar(X(ls), X(ls + 20), y + 56, 38, COL["detect"], "detection ≈20sym", size=18)
        draw_bar(X(ls + 20), X(32), y + 56, 38, COL["accum"], f"accum {accum}", size=18)

    text(1430, y + 84, per, fill=color, size=25)
    text(430, y + 124, detail, fill=color if special == "lead14" else COL["muted"], size=18)


row(6, 345, "4.0%", "result2: sfdto 34, fsl 6 / accum=8", 8, COL["red"])
row(10, 520, "0.4%", "result2: phe 2, fsl 2 / accum=12", 12, COL["orange"])
row(12, 695, "0.0%", "result2: error 0 / 첫 PAC = noise 2sym + preamble 6sym → 안정 성공", 14, COL["green"])
row(14, 870, "3.9%", "result2: sfdto 36, fsl 3 / accum min=8 max=16 avg=9.4 → bimodal 8/16 가설", 16, COL["red"], "lead14")
row(15, 1065, "0.8%", "result2: sfdto 5, phe 1, fsl 2 / discarded PAC 이후 boundary가 밀려 accum 9", 9, COL["orange"], "lead15")

# Notes
rect(82, 1240, 1460, 62 + 1240, "#2b2d2f", radius=14, outline="#4b4f52", width=1)
text(110, 1261, "주의: result2의 lead 6/10/12/14/15 결과만 사용한 해석 도식. lead 2/4는 result2에서 FWTO 100%라 PAC grid 행에서 제외.", size=17)
text(110, 1288, "δ≈11.3us는 lead10 정렬을 기준으로 한 도식용 추정값이며, RF 안정화 + digital acquisition chain을 합친 유효 지연으로 해석.", fill=COL["muted"], size=17)

img = img.resize((W, H), Image.Resampling.LANCZOS)
OUT_DIR.mkdir(parents=True, exist_ok=True)
img.save(OUT)
print(OUT)
