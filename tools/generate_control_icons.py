"""生成控件原创线条SVG源稿及PNG，运行时只用PNG，不要求QtSvg。"""
from pathlib import Path
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parents[1] / 'resources' / 'icons' / 'controls'
root.mkdir(exist_ok=True)
paths = {
    'close': [[(7, 7), (17, 17)], [(17, 7), (7, 17)]],
    'float': [[(9, 7), (9, 4), (20, 4), (20, 15), (17, 15)],
              [(4, 9), (15, 9), (15, 20), (4, 20), (4, 9)]],
    'up': [[(6, 15), (12, 9), (18, 15)]],
    'down': [[(6, 9), (12, 15), (18, 9)]],
    'check': [[(5, 12), (10, 17), (19, 7)]],
}
for name, strokes in paths.items():
    svg = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24">'
    image = Image.new('RGBA', (192, 192))
    draw = ImageDraw.Draw(image)
    for stroke in strokes:
        points = ' '.join(f'{x},{y}' for x, y in stroke)
        svg += f'<polyline points="{points}" fill="none" stroke="#63766E" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/>'
        draw.line([(x * 8, y * 8) for x, y in stroke], fill='#63766E', width=14, joint='curve')
        for x, y in stroke:
            draw.ellipse((x * 8 - 7, y * 8 - 7, x * 8 + 7, y * 8 + 7), fill='#63766E')
    (root / f'{name}.svg').write_text(svg + '</svg>\n', encoding='utf-8')
    image.resize((72, 72), Image.Resampling.LANCZOS).save(root / f'{name}.png')
