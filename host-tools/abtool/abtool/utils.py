from __future__ import annotations

import math
from typing import Tuple


def align(value: int, alignment: int) -> int:
    """Align value up to the next multiple of alignment."""
    if alignment <= 0:
        return value
    return (value + alignment - 1) // alignment * alignment


def parse_hex_color(text: str) -> Tuple[int, int, int]:
    """Parse #RRGGBB or RRGGBB strings into RGB tuple."""
    raw = text.strip().lstrip("#")
    if len(raw) != 6:
        raise ValueError(f"Invalid color '{text}'")
    r = int(raw[0:2], 16)
    g = int(raw[2:4], 16)
    b = int(raw[4:6], 16)
    return r, g, b


def clamp(value: int, min_value: int, max_value: int) -> int:
    """Clamp value within the inclusive range."""
    return max(min_value, min(value, max_value))


def lerp(a: float, b: float, t: float) -> float:
    """Linear interpolation helper."""
    return a + (b - a) * t


def iround(value: float) -> int:
    """Round to nearest integer."""
    return int(math.floor(value + 0.5))

