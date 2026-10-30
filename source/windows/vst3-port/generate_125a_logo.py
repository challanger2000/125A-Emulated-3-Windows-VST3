# Authoritative source: 125A-Branding/125A_Logo_Master_FINAL.svg
# Convert SVG straight-line paths to C++ vertices at build time, without rasterization.
import pathlib, re, sys, urllib.request, xml.etree.ElementTree as ET

url = "https://raw.githubusercontent.com/challanger2000/125A-Branding/main/125A_Logo_Master_FINAL.svg"
source = pathlib.Path(sys.argv[1])
output = pathlib.Path(sys.argv[2])
if not source.is_file():
    source.parent.mkdir(parents=True, exist_ok=True)
    urllib.request.urlretrieve(url, source)
root = ET.parse(source).getroot()
polygons, contours, points = [], [], []
for el in root.iter():
    if el.tag.rsplit("}", 1)[-1] != "path":
        continue
    d = el.attrib["d"]
    tokens = re.findall(r"[MLZ]|-?\d+(?:\.\d+)?", d)
    first_contour = len(contours)
    poly = []
    cmd = None
    def finish():
        if len(poly) > 2:
            contours.append((len(points), len(poly)))
            points.extend(poly)
        poly.clear()
    it = iter(tokens)
    for t in it:
        if t in ("M", "L", "Z"):
            cmd = t
            if t == "Z":
                finish()
            continue
        x, y = int(float(t)), int(float(next(it)))
        if cmd == "M":
            finish()
        poly.append((x, y))
        cmd = "L"
    finish()
    polygons.append((first_contour, len(contours) - first_contour, 2 if el.attrib.get("fill", "").lower() == "#d71920" else 1))
if len(polygons) != 30 or not points:
    raise ValueError("Unexpected authoritative 125A SVG geometry")
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text("""// Generated from the exact authoritative 125A SVG master. Do not edit.
#pragma once
namespace eiii::ui::logo_master {
struct point { float x,y; };
struct ring { unsigned first,count; };
struct shape { unsigned first,count,color; };
inline constexpr point vertices[] = {
""" + ",\n".join(f"{{{x},{y}}}" for x,y in points) + "\n};\ninline constexpr ring rings[]={\n" + ",\n".join(f"{{{a},{b}}}" for a,b in contours) + "\n};\ninline constexpr shape shapes[]={\n" + ",\n".join(f"{{{a},{b},{c}}}" for a,b,c in polygons) + "\n};\n}\n", encoding="utf-8")
