# Loop Screenshot 

# Random info
- Vibe coded!
- Do not support this 
- Helps you screenshot stuff 
- I used Luna 6-High and Sonnet 5.5 at medium 
- Yes, the logo is also AI generated.

Windows-focused Geometry Dash / Geode screenshot mod.

## Features
- Global configurable screenshot keybind (default Ctrl+Shift+S)
- Independent save-to-file and clipboard toggles
- PNG output
- Native, 720p, 1080p, 1440p, 4K, 8K, and custom output sizes
- Bicubic, bilinear, and nearest-neighbor scaling
- Custom output folder, filename prefix, and optional date subfolders
- Optional success/error notifications

## Important resolution note
The mod renders the active Geometry Dash Cocos scene into an offscreen render texture. Output sizes above the game's actual rendered dimensions are upscaled. This changes the pixel dimensions but cannot recreate details that were not present in the original render.

## Build
Use Geode SDK 5.11.2 (mod.json must match the installed SDK exactly), GD 2.2081:
`geode build --ninja`

This project has not been compiled in this environment. Build it locally and share any compiler output so SDK-specific signature differences can be corrected.
