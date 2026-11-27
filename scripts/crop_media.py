#!/usr/bin/env python3
"""Turn the phone photos and video in assets/ into the docs images.

Workstation tool only: the firmware never needs OpenCV or ffmpeg. Usage:

    python3 scripts/crop_media.py

Requires assets/image/*.jpeg and assets/video/*.mp4; writes docs/images/.
The raw assets are git ignored, only the generated files are committed.
"""

import subprocess
import sys
from pathlib import Path

import cv2
import numpy as np

W, H = 320, 240
ROOT = Path(__file__).resolve().parent.parent
SRC_IMAGES = ROOT / "assets" / "image"
SRC_VIDEO = ROOT / "assets" / "video"
OUT = ROOT / "docs" / "images"

# Which photo becomes which docs image, plus the rotation the crop needs. The
# perspective crop is exact, but a photo can hold the panel upside down, so the
# angle is chosen by eye once instead of guessed at runtime.
SCREEN_SHOTS = {
    "WhatsApp Image 2026-11-27 at 7.48.31 AM.jpeg": ("ui-fx", 180),
    "WhatsApp Image 2026-11-27 at 7.48.32 AM (1).jpeg": ("ui-draw", 0),
    "WhatsApp Image 2026-11-27 at 7.48.32 AM (2).jpeg": ("ui-bench", 0),
    "WhatsApp Image 2026-11-27 at 7.48.32 AM.jpeg": ("ui-live", 0),
}

# Photos of the board itself: no screen to straighten, so crop the red PCB.
BOARD_SHOTS = {
    # These two are the TFT module itself, not the ESP32 board.
    "WhatsApp Image 2026-11-27 at 7.48.32 AM (3).jpeg": "panel-back",
    "WhatsApp Image 2026-11-27 at 7.48.33 AM.jpeg": "panel-front",
}

# The ESP32 board fills the frame at an angle, so its crop is a plain box given
# as fractions of the source: left, top, right, bottom.
BOARD_SHOT = ("board-esp", (0.26, 0.35, 0.94, 0.69))
BOARD_SHOT_WIDTH = 640

# The video is handheld, so a per-frame screen crop would flicker. A plain
# scaled clip of the segment where tabs are tapped reads better and stays small.
GIF_SEGMENT = (9, 15)
GIF_WIDTH = 200
GIF_FPS = 8


def find_screen(gray):
    """Largest bright convex quadrilateral, which on these photos is the panel."""
    blurred = cv2.GaussianBlur(gray, (5, 5), 0)
    edges = cv2.dilate(cv2.Canny(blurred, 40, 120), np.ones((3, 3), np.uint8), 1)
    contours, _ = cv2.findContours(edges, cv2.RETR_LIST, cv2.CHAIN_APPROX_SIMPLE)
    contours = sorted(contours, key=cv2.contourArea, reverse=True)[:15]

    area_image = gray.shape[0] * gray.shape[1]
    for contour in contours:
        area = cv2.contourArea(contour)
        if not area_image * 0.05 < area < area_image * 0.35:
            continue
        hull = cv2.convexHull(contour)
        approx = cv2.approxPolyDP(hull, 0.02 * cv2.arcLength(hull, True), True)
        if len(approx) != 4 or not cv2.isContourConvex(approx):
            continue
        x, y, w, h = cv2.boundingRect(approx)
        # A display is much brighter than the desk; this rejects quads made of
        # cables and table edges.
        if gray[y : y + h, x : x + w].mean() < 120:
            continue
        return approx.reshape(4, 2).astype(np.float32)
    return None


def order_corners(points):
    """Sort by angle so the quad never crosses, then start top-left."""
    centre = points.mean(axis=0)
    angles = np.arctan2(points[:, 1] - centre[1], points[:, 0] - centre[0])
    ordered = points[np.argsort(angles)]
    start = int(np.argmin(ordered[:, 0] + ordered[:, 1]))
    return np.roll(ordered, -start, axis=0).astype(np.float32)


def screen_crop(path, angle):
    image = cv2.imread(str(path))
    if image is None:
        return None

    factor = min(1.0, 900 / max(image.shape[:2]))
    small = cv2.resize(image, None, fx=factor, fy=factor) if factor < 1 else image
    quad = find_screen(cv2.cvtColor(small, cv2.COLOR_BGR2GRAY))
    if quad is None:
        return None
    quad = order_corners(quad) / factor

    width = np.linalg.norm(quad[1] - quad[0])
    height = np.linalg.norm(quad[3] - quad[0])
    dst_w, dst_h = (W, H) if width >= height else (H, W)
    dst = np.array([[0, 0], [dst_w - 1, 0], [dst_w - 1, dst_h - 1], [0, dst_h - 1]], np.float32)

    warped = cv2.warpPerspective(
        image, cv2.getPerspectiveTransform(quad, dst), (dst_w, dst_h), flags=cv2.INTER_AREA
    )
    if dst_w != W:
        warped = cv2.rotate(warped, cv2.ROTATE_90_CLOCKWISE)
    if angle:
        warped = cv2.rotate(warped, cv2.ROTATE_180 if angle == 180 else cv2.ROTATE_90_CLOCKWISE)
    return warped


def board_crop(path):
    """Crop the red PCB of the module out of a table shot."""
    image = cv2.imread(str(path))
    if image is None:
        return None
    hsv = cv2.cvtColor(image, cv2.COLOR_BGR2HSV)
    red = cv2.inRange(hsv, (0, 90, 60), (12, 255, 255)) | cv2.inRange(hsv, (170, 90, 60), (180, 255, 255))
    red = cv2.morphologyEx(red, cv2.MORPH_CLOSE, np.ones((15, 15), np.uint8))
    contours, _ = cv2.findContours(red, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    if not contours:
        return None
    x, y, w, h = cv2.boundingRect(max(contours, key=cv2.contourArea))
    margin = int(0.06 * max(w, h))
    x0, y0 = max(0, x - margin), max(0, y - margin)
    x1 = min(image.shape[1], x + w + margin)
    y1 = min(image.shape[0], y + h + margin)
    crop = image[y0:y1, x0:x1]
    scale = 520 / crop.shape[1]
    return cv2.resize(crop, None, fx=scale, fy=scale, interpolation=cv2.INTER_AREA)


def fraction_crop(path, box, width):
    image = cv2.imread(str(path))
    if image is None:
        return None
    h, w = image.shape[:2]
    left, top, right, bottom = box
    crop = image[int(top * h) : int(bottom * h), int(left * w) : int(right * w)]
    scale = width / crop.shape[1]
    return cv2.resize(crop, None, fx=scale, fy=scale, interpolation=cv2.INTER_AREA)


def make_gif(video):
    out = OUT / "demo.gif"
    start, end = GIF_SEGMENT
    filters = (
        f"fps={GIF_FPS},scale={GIF_WIDTH}:-1:flags=lanczos,"
        "split[a][b];[a]palettegen=max_colors=96:stats_mode=diff[p];"
        "[b][p]paletteuse=dither=bayer:bayer_scale=3"
    )
    cmd = [
        "ffmpeg",
        "-v",
        "error",
        "-ss",
        str(start),
        "-t",
        str(end - start),
        "-i",
        str(video),
        "-vf",
        filters,
        "-loop",
        "0",
        "-y",
        str(out),
    ]
    subprocess.run(cmd, check=True)
    return out


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    missing = 0

    for name, (out_name, angle) in SCREEN_SHOTS.items():
        crop = screen_crop(SRC_IMAGES / name, angle)
        if crop is None:
            print(f"MISSED  {name}")
            missing += 1
            continue
        cv2.imwrite(str(OUT / f"{out_name}.jpg"), crop, [cv2.IMWRITE_JPEG_QUALITY, 90])
        print(f"ok      {out_name}.jpg")

    for name, out_name in BOARD_SHOTS.items():
        crop = board_crop(SRC_IMAGES / name)
        if crop is None:
            print(f"MISSED  {name}")
            missing += 1
            continue
        cv2.imwrite(str(OUT / f"{out_name}.jpg"), crop, [cv2.IMWRITE_JPEG_QUALITY, 90])
        print(f"ok      {out_name}.jpg")

    board_source = next(iter(sorted(SRC_IMAGES.glob(f"{BOARD_SHOT[0]}.*"))), None)
    if board_source is not None:
        crop = fraction_crop(board_source, BOARD_SHOT[1], BOARD_SHOT_WIDTH)
        cv2.imwrite(str(OUT / f"{BOARD_SHOT[0]}.jpg"), crop, [cv2.IMWRITE_JPEG_QUALITY, 90])
        print(f"ok      {BOARD_SHOT[0]}.jpg")
    else:
        print(f"note    no {BOARD_SHOT[0]}.* in assets/image, board photo skipped")

    videos = sorted(SRC_VIDEO.glob("*.mp4"))
    if videos:
        out = make_gif(videos[0])
        print(f"ok      {out.name} ({out.stat().st_size // 1024} KB)")
    else:
        print("MISSED  no video for the demo gif")
        missing += 1

    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
