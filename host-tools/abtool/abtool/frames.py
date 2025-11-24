from __future__ import annotations

import logging
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List, Sequence, Tuple

import imageio.v2 as imageio
import numpy as np
from PIL import Image, ImageSequence

from .utils import parse_hex_color

LOG = logging.getLogger(__name__)


@dataclass
class DecodedFrame:
    image: Image.Image
    duration_us: int


def load_media_frames(source: Path, fallback_fps: int) -> List[DecodedFrame]:
    suffix = source.suffix.lower()
    if suffix in {".gif", ".apng", ".png"}:
        return _load_image_sequence(source, fallback_fps)
    if suffix in {".mp4", ".mov", ".mkv", ".avi", ".webm"}:
        return _load_video_frames(source, fallback_fps)
    return _load_single_image(source, fallback_fps)


def _load_image_sequence(path: Path, fallback_fps: int) -> List[DecodedFrame]:
    frames: List[DecodedFrame] = []
    with Image.open(path) as img:
        default_duration = int(1_000_000 / max(1, fallback_fps))
        for idx, frame in enumerate(ImageSequence.Iterator(img)):
            duration = int(frame.info.get("duration", default_duration // 1000) * 1000)
            rgba = frame.convert("RGBA")
            frames.append(DecodedFrame(image=rgba.copy(), duration_us=duration or default_duration))
            LOG.debug("Loaded GIF frame %d (%dus)", idx, frames[-1].duration_us)
    return frames


def _load_video_frames(path: Path, fallback_fps: int) -> List[DecodedFrame]:
    frames: List[DecodedFrame] = []
    reader = imageio.get_reader(path)
    meta = reader.get_meta_data()
    fps = float(meta.get("fps", fallback_fps))
    duration_us = int(1_000_000 / max(1.0, fps))
    for idx, frame in enumerate(reader):
        image = Image.fromarray(frame).convert("RGBA")
        frames.append(DecodedFrame(image=image, duration_us=duration_us))
        LOG.debug("Loaded video frame %d", idx)
    reader.close()
    return frames


def _load_single_image(path: Path, fallback_fps: int) -> List[DecodedFrame]:
    image = Image.open(path).convert("RGBA")
    duration = int(1_000_000 / max(1, fallback_fps))
    return [DecodedFrame(image=image, duration_us=duration)]


def resize_frame(
    frame: Image.Image,
    width: int,
    height: int,
    scaling: str,
    background_hex: str,
) -> Image.Image:
    target = Image.new("RGBA", (width, height), _background_rgba(background_hex))
    src_w, src_h = frame.size
    if scaling == "center":
        resized = frame.copy()
    else:
        if scaling == "fill":
            ratio = max(width / src_w, height / src_h)
        else:
            ratio = min(width / src_w, height / src_h)
        ratio = max(ratio, 1e-6)
        new_size = (int(src_w * ratio), int(src_h * ratio))
        resized = frame.resize(new_size, Image.Resampling.LANCZOS)
    crop_w = min(resized.size[0], width)
    crop_h = min(resized.size[1], height)
    crop_left = max(0, (resized.size[0] - crop_w) // 2)
    crop_top = max(0, (resized.size[1] - crop_h) // 2)
    cropped = resized.crop((crop_left, crop_top, crop_left + crop_w, crop_top + crop_h))
    offset_x = max(0, (width - crop_w) // 2)
    offset_y = max(0, (height - crop_h) // 2)
    target.paste(cropped, (offset_x, offset_y), cropped)
    return target


def _background_rgba(text: str) -> Tuple[int, int, int, int]:
    r, g, b = parse_hex_color(text)
    return (r, g, b, 255)


def export_frames_to_bmp(frames: Sequence[DecodedFrame], outdir: Path, prefix: str = "frame") -> List[Path]:
    outdir.mkdir(parents=True, exist_ok=True)
    output_paths: List[Path] = []
    for idx, decoded in enumerate(frames, start=1):
        filename = f"{prefix}{idx:04d}.bmp"
        path = outdir / filename
        decoded.image.save(path, format="BMP")
        output_paths.append(path)
    return output_paths

