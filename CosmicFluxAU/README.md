# CosmicFluxAU

iOS AUv3 Audio Unit Host Application and Extension

## Project Structure

- **Design/** - Icon design assets
  - `icon.svg` - Master SVG icon with dreamy purple-and-black space aesthetic
  - `render_icons.py` - Script to render all icon sizes from SVG
- **HostApp/** - Main iOS host application
  - `Assets.xcassets/AppIcon.appiconset/` - App icon assets in all required sizes
  - `AppDelegate.swift` - Application delegate
  - `ViewController.swift` - Main view controller
  - `Info.plist` - Host app configuration
- **Extension/** - AUv3 Audio Unit extension
  - `AudioUnitViewController.swift` - Audio unit UI controller
  - `CosmicFluxAUAudioUnit.swift` - Audio unit implementation
  - `Info.plist` - Extension configuration
- `project.yml` - XcodeGen configuration
- `icon_preview.png` - Preview of the app icon (512x512)

## Icon Design

The app icon features a dreamy, hazy purple-and-black space aesthetic with:
- Soft nebula glow effects
- Layered haze with blur filters
- Subtle star field
- Deep black background
- No text (name-agnostic design)
- Optimized for readability at small sizes

## Building the Project

### Prerequisites

- Xcode 12.0 or later
- XcodeGen (install via `brew install xcodegen`)
- Python 3 with cairosvg (for icon rendering)

### Generate Xcode Project

```bash
cd CosmicFluxAU
xcodegen
```

This will generate `CosmicFluxAU.xcodeproj` from `project.yml`.

### Regenerate Icons

To regenerate icons from the SVG master:

```bash
cd CosmicFluxAU/Design
python3 render_icons.py
```

This will:
1. Render all required iOS icon sizes from `icon.svg`
2. Generate the `Contents.json` file for the asset catalog
3. Create a preview PNG at the project root

## Icon Assets

The following icon sizes are included:

- **App Store**: 1024x1024 (1x)
- **iPhone**: 20pt, 29pt, 40pt, 60pt (2x and 3x)
- **iPad**: 20pt, 29pt, 40pt, 76pt (1x and 2x), 83.5pt (2x)

All icons are referenced in the `ASSETCATALOG_COMPILER_APPICON_NAME` build setting in `project.yml`.

## License

See LICENSE file in repository root.
