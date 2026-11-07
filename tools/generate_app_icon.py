"""从既有原创嫩芽图形生成应用SVG、PNG与多尺寸ICO，无运行时SVG依赖。"""
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
ICON = ROOT / "resources" / "icons"
SVG = '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24">
<path d="M12 20 C2 18 2 9 3 6 C12 6 14 12 12 20 M12 15 C12 6 17 3 22 3 C22 11 18 15 12 15" fill="#DDF2E7" stroke="#247A62" stroke-width="1.7" stroke-linejoin="round"/>
<path d="M12 22 V13" fill="none" stroke="#247A62" stroke-width="1.7" stroke-linecap="round"/>
</svg>\n'''

def curve(points):
    result = []
    for index in range(101):
        t = index / 100
        result.append(tuple(sum(((1-t)**3, 3*(1-t)**2*t, 3*(1-t)*t*t, t**3)[i] * points[i][axis] for i in range(4)) for axis in range(2)))
    return result

def main():
    ICON.mkdir(exist_ok=True)
    (ICON / "app.svg").write_text(SVG, encoding="utf-8")
    scale = 1024 / 24
    picture = Image.new("RGBA", (1024, 1024))
    draw = ImageDraw.Draw(picture)
    paths = [curve([(12,20),(2,18),(2,9),(3,6)]) + curve([(3,6),(12,6),(14,12),(12,20)]),
             curve([(12,15),(12,6),(17,3),(22,3)]) + curve([(22,3),(22,11),(18,15),(12,15)])]
    for path in paths:
        path = [(round(x*scale), round(y*scale)) for x,y in path]
        draw.polygon(path, fill="#DDF2E7")
        draw.line(path, fill="#247A62", width=round(1.7*scale), joint="curve")
    width = round(1.7*scale)
    draw.line([(12*scale,22*scale),(12*scale,13*scale)], fill="#247A62", width=width)
    for y in (22,13):
        draw.ellipse((12*scale-width/2,y*scale-width/2,12*scale+width/2,y*scale+width/2),fill="#247A62")
    picture = picture.resize((512,512), Image.Resampling.LANCZOS)
    picture.save(ICON / "app.png")
    picture.save(ICON / "app.ico", sizes=[(16,16),(32,32),(48,48),(256,256)])

if __name__ == "__main__":
    main()
