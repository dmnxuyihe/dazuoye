"""Build the map webfont with all glyphs from the bundled OFL Chinese font.

Station names, geocoded addresses and route steps come from the API, so a
subset based on source strings or test fixtures cannot cover the map text.
"""
from pathlib import Path
from fontTools.ttLib import TTFont

root = Path(__file__).resolve().parents[1]
font = TTFont(root / 'desktop/assets/noto-sans-sc.ttf')
font.flavor = 'woff2'
font.save(root / 'desktop/map/map-labels.woff2')
