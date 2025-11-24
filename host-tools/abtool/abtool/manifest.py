from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional

DEFAULT_FPS = 24
DEFAULT_WIDTH = 640
DEFAULT_HEIGHT = 360
DEFAULT_FRAME_DURATION_US = int(1_000_000 / DEFAULT_FPS)
DEFAULT_MAX_MEMORY = 64 * 1024 * 1024


@dataclass
class FrameEntry:
    path: Path
    duration_us: int


@dataclass
class Manifest:
    logical_width: int = DEFAULT_WIDTH
    logical_height: int = DEFAULT_HEIGHT
    scaling: str = "letterbox"
    background: str = "#000000"
    max_memory: int = DEFAULT_MAX_MEMORY
    loop_count: int = 1
    frame_duration_us: int = DEFAULT_FRAME_DURATION_US
    allow_key_skip: bool = True
    max_total_duration_ms: int = 0
    frames: List[FrameEntry] = field(default_factory=list)

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "Manifest":
        frames = [
            FrameEntry(path=Path(item["path"]), duration_us=int(item.get("duration_us", 0)))
            for item in data.get("frames", [])
        ]
        return cls(
            logical_width=int(data.get("logical_width", DEFAULT_WIDTH)),
            logical_height=int(data.get("logical_height", DEFAULT_HEIGHT)),
            scaling=data.get("scaling", "letterbox"),
            background=data.get("background", "#000000"),
            max_memory=int(data.get("max_memory", DEFAULT_MAX_MEMORY)),
            loop_count=int(data.get("loop_count", 1)),
            frame_duration_us=int(data.get("frame_duration_us", DEFAULT_FRAME_DURATION_US)),
            allow_key_skip=bool(data.get("allow_key_skip", True)),
            max_total_duration_ms=int(data.get("max_total_duration_ms", 0)),
            frames=frames,
        )

    def to_dict(self) -> Dict[str, Any]:
        return {
            "logical_width": self.logical_width,
            "logical_height": self.logical_height,
            "scaling": self.scaling,
            "background": self.background,
            "max_memory": self.max_memory,
            "loop_count": self.loop_count,
            "frame_duration_us": self.frame_duration_us,
            "allow_key_skip": self.allow_key_skip,
            "max_total_duration_ms": self.max_total_duration_ms,
            "frames": [
                {"path": str(entry.path).replace("\\", "/"), "duration_us": entry.duration_us}
                for entry in self.frames
            ],
        }

    def ensure_frames(self) -> None:
        if not self.frames:
            raise ValueError("Manifest does not contain any frames.")


def load_manifest(path: Path) -> Manifest:
    data = json.loads(path.read_text(encoding="utf-8"))
    manifest = Manifest.from_dict(data)
    manifest.ensure_frames()
    return manifest


def save_manifest(path: Path, manifest: Manifest) -> None:
    path.write_text(json.dumps(manifest.to_dict(), indent=2), encoding="utf-8")


def build_manifest_from_frames(
    frames: Iterable[FrameEntry],
    width: int,
    height: int,
    fps: int,
    scaling: str,
    background: str,
) -> Manifest:
    entries = list(frames)
    for entry in entries:
        if entry.duration_us <= 0:
            entry.duration_us = int(1_000_000 / max(1, fps))
    manifest = Manifest(
        logical_width=width,
        logical_height=height,
        scaling=scaling,
        background=background,
        frames=entries,
        loop_count=1,
        frame_duration_us=int(1_000_000 / max(1, fps)),
    )
    return manifest

