#!/usr/bin/env python3
"""Render the draw.io architecture file to SVG for the README.

The .drawio file stays the source of truth and is what you edit; this only draws
the subset used there (swimlanes, rounded boxes, cylinders and orthogonal edges)
so the README can show the same diagram without a draw.io install.

    python3 scripts/drawio_to_svg.py docs/diagrams/architecture.drawio docs/images/architecture.svg
"""

import sys
import xml.etree.ElementTree as ET
from pathlib import Path

FONT = "Inter, 'Segoe UI', system-ui, sans-serif"


def collect(mxfile):
    """Walk every mxCell and return vertices and edges with absolute geometry."""
    cells = {}
    for cell in mxfile.iter("mxCell"):
        cid = cell.get("id")
        if cid is None:
            continue
        geom = cell.find("mxGeometry")
        box = None
        if geom is not None:
            box = (
                float(geom.get("x", 0)),
                float(geom.get("y", 0)),
                float(geom.get("width", 0)),
                float(geom.get("height", 0)),
            )
        cells[cid] = {
            "id": cid,
            "parent": cell.get("parent"),
            "value": cell.get("value") or "",
            "style": cell.get("style") or "",
            "box": box,
            "vertex": cell.get("vertex") == "1",
            "edge": cell.get("edge") == "1",
            "source": cell.get("source"),
            "target": cell.get("target"),
        }
    return cells


def absolute(cells, cell):
    x, y, w, h = cell["box"]
    parent = cell["parent"]
    while parent and parent in cells and cells[parent]["box"] is not None:
        px, py, _, _ = cells[parent]["box"]
        x += px
        y += py
        parent = cells[parent]["parent"]
    return x, y, w, h


def style_map(style):
    out = {}
    for part in style.split(";"):
        if "=" in part:
            key, value = part.split("=", 1)
            out[key] = value
        elif part:
            out[part] = "1"
    return out


def colour(style, key, fallback):
    value = style.get(key, fallback)
    return value if value.startswith("#") else fallback


def escape(text):
    return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def label_lines(value):
    return [escape(part) for part in value.replace("<br>", "\n").split("\n")]


def text_block(cx, cy, lines, size, weight="400", fill="#1f2937"):
    total = len(lines) * (size + 2)
    out = []
    for i, line in enumerate(lines):
        y = cy - total / 2 + (i + 1) * (size + 2) - 2
        out.append(
            f'<text x="{cx:.1f}" y="{y:.1f}" font-family="{FONT}" font-size="{size}" '
            f'font-weight="{weight}" fill="{fill}" text-anchor="middle">{line}</text>'
        )
    return out


def cylinder(x, y, w, h, style):
    fill, stroke = colour(style, "fillColor", "#ffffff"), colour(style, "strokeColor", "#333333")
    ry = min(10.0, h / 4)
    return [
        f'<path d="M{x:.1f},{y + ry:.1f} a{w / 2:.1f},{ry:.1f} 0 0 1 {w:.1f},0 '
        f'v{h - 2 * ry:.1f} a{w / 2:.1f},{ry:.1f} 0 0 1 -{w:.1f},0 z" '
        f'fill="{fill}" stroke="{stroke}" stroke-width="1.2"/>',
        f'<path d="M{x:.1f},{y + ry:.1f} a{w / 2:.1f},{ry:.1f} 0 0 0 {w:.1f},0" '
        f'fill="none" stroke="{stroke}" stroke-width="1.2"/>',
    ]


def longest_midpoint(points):
    """Where an edge label reads best: the middle of its longest segment."""
    best, best_len, at = None, -1, None
    for a, b in zip(points, points[1:]):
        length = ((b[0] - a[0]) ** 2 + (b[1] - a[1]) ** 2) ** 0.5
        if length > best_len:
            # Vertical runs carry the label above the middle so it clears any
            # shape sitting beside the wire.
            t = 0.3 if abs(b[1] - a[1]) > abs(b[0] - a[0]) else 0.5
            best, best_len = (a, b), length
            at = (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)
    return at


def swimlane(x, y, w, h, style):
    fill, stroke = colour(style, "fillColor", "#ffffff"), colour(style, "strokeColor", "#333333")
    return [
        f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" rx="6" '
        f'fill="{fill}" stroke="{stroke}" stroke-width="1.4"/>',
        f'<path d="M{x:.1f},{y + 28:.1f} h{w:.1f}" stroke="{stroke}" stroke-width="1.2"/>',
    ]


def edge_route(cells, cell):
    source, target = cells.get(cell["source"]), cells.get(cell["target"])
    if not source or not target or source["box"] is None or target["box"] is None:
        return None
    sx, sy, sw, sh = absolute(cells, source)
    tx, ty, tw, th = absolute(cells, target)
    style = style_map(cell["style"])
    # A top-down flow leaves a box from its bottom edge; using the centre would
    # draw the wire straight through the label.
    exit_x, exit_y = float(style.get("exitX", 0.5)), float(style.get("exitY", 1.0))
    entry_x, entry_y = float(style.get("entryX", 0.5)), float(style.get("entryY", 0.0))
    start = (sx + sw * exit_x, sy + sh * exit_y)
    end = (tx + tw * entry_x, ty + th * entry_y)

    if abs(exit_y - 0.5) > 0.01:  # leaves from the top or bottom
        mid = (start[1] + end[1]) / 2
        points = [start, (start[0], mid), (end[0], mid), end]
    else:  # leaves from a side
        mid = (start[0] + end[0]) / 2
        points = [start, (mid, start[1]), (mid, end[1]), end]
    return points, style


def arrow_head(points):
    (x0, y0), (x1, y1) = points[-2], points[-1]
    dx, dy = x1 - x0, y1 - y0
    length = (dx * dx + dy * dy) ** 0.5 or 1
    ux, uy = dx / length, dy / length
    ax, ay = x1 - ux * 8, y1 - uy * 8
    left = (ax - uy * 4, ay + ux * 4)
    right = (ax + uy * 4, ay - ux * 4)
    return f'<path d="M{x1:.1f},{y1:.1f} L{left[0]:.1f},{left[1]:.1f} L{right[0]:.1f},{right[1]:.1f} z" fill="#475569"/>'


def render(path):
    root = ET.parse(path).getroot()
    cells_map = collect(root)

    min_x = min_y = 10**9
    max_x = max_y = -10**9
    for cell in cells_map.values():
        if cell["vertex"] and cell["box"] is not None:
            x, y, w, h = absolute(cells_map, cell)
            min_x, min_y = min(min_x, x), min(min_y, y)
            max_x, max_y = max(max_x, x + w), max(max_y, y + h)

    pad = 14
    width, height = max_x - min_x + 2 * pad, max_y - min_y + 2 * pad
    body = [f'<rect width="{width:.0f}" height="{height:.0f}" fill="#ffffff"/>']
    body.append(f'<g transform="translate({pad - min_x:.1f},{pad - min_y:.1f})">')

    for cell in cells_map.values():
        if not cell["vertex"] or cell["box"] is None:
            continue
        style = style_map(cell["style"])
        x, y, w, h = absolute(cells_map, cell)
        if "swimlane" in cell["style"]:
            body += swimlane(x, y, w, h, style)
            body += text_block(x + w / 2, y + 14, label_lines(cell["value"]), 12, "600")
        elif style.get("shape") == "cylinder3":
            body += cylinder(x, y, w, h, style)
            body += text_block(x + w / 2, y + h / 2 + 4, label_lines(cell["value"]), 10)
        elif style.get("fillColor") == "none" and style.get("strokeColor") == "none":
            body += text_block(x + w / 2, y + h / 2, label_lines(cell["value"]), 10, "400", "#475569")
        else:
            fill, stroke = colour(style, "fillColor", "#ffffff"), colour(style, "strokeColor", "#333333")
            body.append(
                f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" rx="8" '
                f'fill="{fill}" stroke="{stroke}" stroke-width="1.2"/>'
            )
            body += text_block(x + w / 2, y + h / 2, label_lines(cell["value"]), 11)

    for cell in cells_map.values():
        if not cell["edge"]:
            continue
        route = edge_route(cells_map, cell)
        if route is None:
            continue
        points, style = route
        dash = ' stroke-dasharray="5 4"' if "dashed=1" in cell["style"] else ""
        path = " ".join(f"{p[0]:.1f},{p[1]:.1f}" for p in points)
        body.append(
            f'<polyline points="{path}" fill="none" stroke="#475569" stroke-width="1.4"{dash}/>'
        )
        body.append(arrow_head(points))
        if cell["value"]:
            mid = longest_midpoint(points)
            body.append(
                f'<text x="{mid[0]:.1f}" y="{mid[1] - 5:.1f}" font-family="{FONT}" font-size="10" '
                f'fill="#475569" text-anchor="middle" paint-order="stroke" stroke="#ffffff" '
                f'stroke-width="3">{escape(cell["value"])}</text>'
            )

    body.append("</g>")
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width:.0f}" height="{height:.0f}" '
        f'viewBox="0 0 {width:.0f} {height:.0f}">' + "".join(body) + "</svg>\n"
    )


def main(source, target):
    svg = render(source)
    Path(target).write_text(svg)
    print(f"wrote {target} ({len(svg) // 1024} KB)")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1], sys.argv[2]))
