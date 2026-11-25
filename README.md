# Loop Screenshot Studio

Windows-only Geometry Dash / Geode screenshot mod.

## Random info
- Vibe coded! Do not support this.
- Made with Luna 6-High and Sonnet 5.5 at medium.
- The logo is AI generated too.

## Features
- Global configurable screenshot keybind (default Ctrl+Shift+S)
- Independent save-to-file and clipboard toggles
- PNG output
- Native, 720p, 1080p, 1440p, 4K, 8K, and custom output sizes
- Bicubic, bilinear, and nearest-neighbor scaling
- Custom output folder, filename prefix, and optional date subfolders
- Optional success/error notifications

## Resolution note
The mod renders the active Geometry Dash scene into an offscreen render texture.
Output sizes above the game's rendered size are upscaled: the pixel count changes,
but no new detail appears.

## Build
Requires Geode CLI 4.0.0+, Geode SDK 6.0.0 (the `geode` field in `mod.json`
must match the installed SDK exactly) and Geometry Dash 2.2091.

    geode sdk update            # or: git checkout v6.0.0 inside %GEODE_SDK%
    geode sdk install-binaries
    geode build --ninja
