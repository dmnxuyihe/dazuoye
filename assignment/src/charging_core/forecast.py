"""Serve a packaged, reproducible historical experiment; never train in requests."""

import json
from functools import lru_cache
from pathlib import Path


@lru_cache(maxsize=1)
def load_forecast():
    path = Path(__file__).with_name("data") / "load_forecast.json"
    if not path.exists():
        return None
    return json.loads(path.read_text(encoding="utf-8"))
