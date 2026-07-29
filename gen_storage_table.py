"""生成连续采样可存时长表 (xlsx)。前提：可用缓冲≈100KB，每点 float=4B。"""
from openpyxl import Workbook

wb = Workbook()

# ---- Sheet1: 每通道点数 ----
ws1 = wb.active
ws1.title = "点数"
ws1.append(["通道数", "点数/通道"])
for ch, pts in [(1, 25600), (2, 12800), (4, 6400), (8, 3200)]:
    ws1.append([ch, pts])

# ---- Sheet2: 可存时长(秒) ----
ws2 = wb.create_sheet("可存秒数")
ws2.append(["粒度", "1通道", "2通道", "4通道", "8通道"])
rows = [
    ["100us(最细)", 2.56, 1.28, 0.64, 0.32],
    ["200us", 5.12, 2.56, 1.28, 0.64],
    ["500us", 12.8, 6.4, 3.2, 1.6],
    ["1ms", 25.6, 12.8, 6.4, 3.2],
    ["10ms", 256, 128, 64, 32],
    ["100ms", 2560, 1280, 640, 320],
]
for r in rows:
    ws2.append(r)

out = r"D:\hardware\lightpowerconsume\连续采样可存时长表.xlsx"
wb.save(out)
print("saved:", out)
