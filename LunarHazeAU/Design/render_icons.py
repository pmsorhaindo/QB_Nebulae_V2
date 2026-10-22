#!/usr/bin/env python3
"""Render the Lunar Haze app icon from Design/icon.svg.

Writes an opaque PNG set into App/Assets.xcassets/AppIcon.appiconset,
including the 1024x1024 iOS marketing icon and the iPhone/iPad sizes
the asset catalog still lists. Preview output is opt-in via --preview
and is not written into the project tree by default.

Requires: pip install cairosvg pillow
"""

import argparse
import io
import json
import sys
from pathlib import Path

try:
    import cairosvg
except ImportError:
    print("cairosvg is required: pip install cairosvg pillow", file=sys.stderr)
    sys.exit(1)

try:
    from PIL import Image
except ImportError:
    print("pillow is required: pip install cairosvg pillow", file=sys.stderr)
    sys.exit(1)

# (point size, scale, idiom). 1024 is the single-size iOS marketing icon.
ICON_SIZES = [
    (1024, 1, "ios-marketing"),
    (20, 2, "iphone"),
    (20, 3, "iphone"),
    (29, 2, "iphone"),
    (29, 3, "iphone"),
    (40, 2, "iphone"),
    (40, 3, "iphone"),
    (60, 2, "iphone"),
    (60, 3, "iphone"),
    (20, 1, "ipad"),
    (20, 2, "ipad"),
    (29, 1, "ipad"),
    (29, 2, "ipad"),
    (40, 1, "ipad"),
    (40, 2, "ipad"),
    (76, 1, "ipad"),
    (76, 2, "ipad"),
    (83.5, 2, "ipad"),
]


def filename_for(size, scale):
    scale_label = f"{int(scale)}x" if float(scale).is_integer() else f"{scale}x"
    if scale == 1:
        return f"icon_{size}x{size}.png"
    return f"icon_{size}x{size}@{scale_label}.png"


def render_png(svg_path, pixel_size):
    raw = cairosvg.svg2png(
        url=str(svg_path),
        output_width=pixel_size,
        output_height=pixel_size,
    )
    image = Image.open(io.BytesIO(raw)).convert("RGBA")
    # App Store icons must be opaque. Flatten any filter fringe onto black.
    flat = Image.new("RGB", image.size, (0, 0, 0))
    flat.paste(image, mask=image.split()[-1])
    buf = io.BytesIO()
    flat.save(buf, format="PNG")
    return buf.getvalue()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--preview",
        type=Path,
        help="Also write a 512px opaque preview PNG to this path",
    )
    args = parser.parse_args()

    script_dir = Path(__file__).resolve().parent
    svg_path = script_dir / "icon.svg"
    assets_root = script_dir.parent / "App" / "Assets.xcassets"
    iconset = assets_root / "AppIcon.appiconset"

    if not svg_path.is_file():
        print(f"missing SVG master: {svg_path}", file=sys.stderr)
        sys.exit(1)

    iconset.mkdir(parents=True, exist_ok=True)
    assets_root.joinpath("Contents.json").write_text(
        json.dumps({"info": {"author": "xcode", "version": 1}}, indent=2) + "\n"
    )

    images = []
    written = set()
    for size, scale, idiom in ICON_SIZES:
        name = filename_for(size, scale)
        pixel_size = int(round(size * scale))
        if name not in written:
            (iconset / name).write_bytes(render_png(svg_path, pixel_size))
            written.add(name)
            print(f"{name} ({pixel_size}px)")
        images.append(
            {
                "filename": name,
                "idiom": idiom,
                "scale": f"{int(scale)}x" if float(scale).is_integer() else f"{scale}x",
                "size": f"{size}x{size}",
            }
        )

    contents = {"images": images, "info": {"author": "xcode", "version": 1}}
    (iconset / "Contents.json").write_text(json.dumps(contents, indent=2) + "\n")
    print(f"wrote {len(written)} icons to {iconset}")

    if args.preview:
        args.preview.parent.mkdir(parents=True, exist_ok=True)
        args.preview.write_bytes(render_png(svg_path, 512))
        print(f"preview {args.preview}")


if __name__ == "__main__":
    main()
