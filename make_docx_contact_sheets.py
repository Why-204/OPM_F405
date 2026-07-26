from pathlib import Path
from PIL import Image, ImageOps, ImageDraw

root = Path(r"D:\quantization\_qa")
pages = sorted(root.glob("page-*.png"))
for group_index in range(0, len(pages), 6):
    batch = pages[group_index:group_index + 6]
    thumbs = []
    for path in batch:
        im = Image.open(path).convert("RGB")
        im.thumbnail((408, 528))
        canvas = Image.new("RGB", (428, 568), "white")
        canvas.paste(im, ((428-im.width)//2, 24))
        ImageDraw.Draw(canvas).text((12, 6), path.stem, fill="black")
        thumbs.append(canvas)
    sheet = Image.new("RGB", (428*3, 568*2), "#d8d8d8")
    for idx, im in enumerate(thumbs):
        sheet.paste(im, ((idx % 3)*428, (idx // 3)*568))
    sheet.save(root / f"contact-{group_index//6+1}.png")
