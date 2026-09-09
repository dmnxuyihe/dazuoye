"""Subset the existing OFL font for the embedded map's controls and station labels."""
import json
from pathlib import Path
from fontTools import subset
root = Path(__file__).resolve().parents[1]
text = ''.join(p.read_text() for p in (root/'desktop/src').glob('*.cpp'))
text += (root/'desktop/map/map.html').read_text()
text += json.dumps(json.loads((root/'desktop/tests/fixtures/stations.json').read_text()), ensure_ascii=False)
text += ''.join(chr(n) for n in range(32, 127))
options = subset.Options()
options.flavor = 'woff2'
font = subset.load_font(str(root/'desktop/assets/noto-sans-sc.ttf'), options)
builder = subset.Subsetter(options=options)
builder.populate(text=text)
builder.subset(font)
subset.save_font(font, str(root/'desktop/map/map-labels.woff2'), options)
