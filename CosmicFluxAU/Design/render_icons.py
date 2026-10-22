#!/usr/bin/env python3
"""
Render app icons from SVG master to all required iOS sizes.
This script generates all icon sizes needed for iOS apps including the 1024x1024 App Store icon.
"""

import os
import sys
from pathlib import Path

try:
    import cairosvg
except ImportError:
    print("Error: cairosvg not installed. Run: pip3 install cairosvg")
    sys.exit(1)

# Define all required iOS icon sizes
# Format: (size, scale, idiom)
ICON_SIZES = [
    # iOS App Store
    (1024, 1, "ios-marketing"),
    
    # iPhone
    (20, 2, "iphone"),
    (20, 3, "iphone"),
    (29, 2, "iphone"),
    (29, 3, "iphone"),
    (40, 2, "iphone"),
    (40, 3, "iphone"),
    (60, 2, "iphone"),
    (60, 3, "iphone"),
    
    # iPad
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

def render_icon(svg_path, output_dir, size, scale, idiom):
    """Render SVG to PNG at specified size and scale."""
    pixel_size = int(size * scale)
    filename = f"icon_{size}x{size}@{scale}x.png" if scale > 1 else f"icon_{size}x{size}.png"
    output_path = output_dir / filename
    
    print(f"Rendering {filename} ({pixel_size}x{pixel_size}px)...")
    
    cairosvg.svg2png(
        url=str(svg_path),
        write_to=str(output_path),
        output_width=pixel_size,
        output_height=pixel_size
    )
    
    return filename, size, scale, idiom

def generate_contents_json(icon_files):
    """Generate Contents.json for the AppIcon.appiconset."""
    images = []
    
    for filename, size, scale, idiom in icon_files:
        images.append({
            "filename": filename,
            "idiom": idiom,
            "scale": f"{int(scale)}x",
            "size": f"{size}x{size}"
        })
    
    contents = {
        "images": images,
        "info": {
            "author": "xcode",
            "version": 1
        }
    }
    
    return contents

def main():
    # Paths
    script_dir = Path(__file__).parent
    svg_path = script_dir / "icon.svg"
    output_dir = script_dir.parent / "HostApp" / "Assets.xcassets" / "AppIcon.appiconset"
    
    # Verify SVG exists
    if not svg_path.exists():
        print(f"Error: SVG file not found at {svg_path}")
        sys.exit(1)
    
    # Create output directory
    output_dir.mkdir(parents=True, exist_ok=True)
    
    print(f"Rendering icons from {svg_path}")
    print(f"Output directory: {output_dir}")
    print("-" * 60)
    
    # Render all icon sizes
    icon_files = []
    for size, scale, idiom in ICON_SIZES:
        filename, s, sc, idi = render_icon(svg_path, output_dir, size, scale, idiom)
        icon_files.append((filename, s, sc, idi))
    
    # Generate Contents.json
    import json
    contents = generate_contents_json(icon_files)
    contents_path = output_dir / "Contents.json"
    
    with open(contents_path, 'w') as f:
        json.dump(contents, f, indent=2)
    
    print("-" * 60)
    print(f"✓ Generated {len(icon_files)} icon files")
    print(f"✓ Created {contents_path}")
    print("\nIcon rendering complete!")
    
    # Also create a preview PNG at 512x512 for the PR
    preview_dir = script_dir.parent
    preview_path = preview_dir / "icon_preview.png"
    print(f"\nCreating preview image at {preview_path}...")
    cairosvg.svg2png(
        url=str(svg_path),
        write_to=str(preview_path),
        output_width=512,
        output_height=512
    )
    print("✓ Preview image created")

if __name__ == "__main__":
    main()
