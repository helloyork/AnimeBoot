from __future__ import annotations

import io
import json
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List, Sequence

from PIL import Image

from .manifest import FrameEntry, Manifest
from .utils import align

MAGIC = b"ABANIM\x00"
HEADER_STRUCT = struct.Struct("<8sHHHHIIIIIIIIII6I")
FRAME_STRUCT = struct.Struct("<QII")
ALIGNMENT = 32


@dataclass
class FramePayload:
    path: Path
    data: bytes
    duration_us: int


def _detect_pixel_format(path: Path) -> int:
    suffix = path.suffix.lower()
    if suffix == ".raw":
        return 0
    if suffix == ".bmp":
        return 1
    return 1


def build_package(manifest: Manifest, root_dir: Path, output: Path) -> None:
    manifest.ensure_frames()
    frames = _load_frames(manifest.frames, root_dir)
    pixel_format = _detect_pixel_format(frames[0].path)
    manifest_dict = manifest.to_dict()
    manifest_bytes = json.dumps(manifest_dict, separators=(",", ":")).encode("utf-8")

    frame_table_offset = align(HEADER_STRUCT.size + len(manifest_bytes), ALIGNMENT)
    frame_data_offset = align(frame_table_offset + len(frames) * FRAME_STRUCT.size, ALIGNMENT)

    target_fps = 0
    if manifest.frame_duration_us:
        target_fps = int(round(1_000_000 / manifest.frame_duration_us))
    flags = 0
    if manifest_bytes:
        flags |= 0x1
    if pixel_format == 0:
        flags |= 0x2
    header = HEADER_STRUCT.pack(
        MAGIC,
        1,
        0,
        HEADER_STRUCT.size,
        flags,
        len(manifest_bytes),
        len(frames),
        frame_table_offset,
        frame_data_offset,
        manifest.logical_width,
        manifest.logical_height,
        pixel_format,
        target_fps,
        manifest.loop_count,
        0,
        0,
        0,
        0,
        0,
        0,
    )

    with output.open("wb") as fp:
        fp.write(header)
        fp.write(manifest_bytes)
        fp.write(b"\x00" * (frame_table_offset - HEADER_STRUCT.size - len(manifest_bytes)))

        table_bytes = bytearray()
        cursor = 0
        for frame in frames:
            table_bytes += FRAME_STRUCT.pack(cursor, len(frame.data), frame.duration_us)
            cursor += len(frame.data)
        fp.write(table_bytes)
        fp.write(b"\x00" * (frame_data_offset - frame_table_offset - len(table_bytes)))
        for frame in frames:
            fp.write(frame.data)


def _load_frames(entries: Sequence[FrameEntry], root_dir: Path) -> List[FramePayload]:
    payloads: List[FramePayload] = []
    for entry in entries:
        path = (root_dir / entry.path).resolve()
        data = path.read_bytes()
        payloads.append(FramePayload(path=entry.path, data=data, duration_us=entry.duration_us))
    return payloads


@dataclass
class LoadedFrame:
    image: Image.Image
    duration_us: int


@dataclass
class LoadedPackage:
    manifest: Manifest
    frames: List[LoadedFrame]
    width: int
    height: int
    pixel_format: int


def load_package(path: Path) -> LoadedPackage:
    with path.open("rb") as fp:
        header_data = fp.read(HEADER_STRUCT.size)
        header = HEADER_STRUCT.unpack(header_data)
        if header[0] != MAGIC:
            raise ValueError("Invalid magic")
        manifest_size = header[5]
        frame_count = header[6]
        frame_table_offset = header[7]
        frame_data_offset = header[8]
        width = header[9]
        height = header[10]
        pixel_format = header[11]

        manifest_bytes = fp.read(manifest_size)
        manifest = Manifest.from_dict(json.loads(manifest_bytes.decode("utf-8")))

        fp.seek(frame_table_offset)
        descriptors = [
            FRAME_STRUCT.unpack(fp.read(FRAME_STRUCT.size)) for _ in range(frame_count)
        ]
        frames: List[LoadedFrame] = []
        for offset, length, duration_us in descriptors:
            fp.seek(frame_data_offset + offset)
            payload = fp.read(length)
            image = _decode_frame(payload, width, height, pixel_format)
            frames.append(LoadedFrame(image=image, duration_us=duration_us))
        return LoadedPackage(
            manifest=manifest,
            frames=frames,
            width=width,
            height=height,
            pixel_format=pixel_format,
        )


def _decode_frame(payload: bytes, width: int, height: int, pixel_format: int) -> Image.Image:
    if pixel_format == 0:
        return Image.frombuffer("RGBA", (width, height), payload, "raw", "BGRA", 0, 1)  # type: ignore
    return Image.open(io.BytesIO(payload)).convert("RGBA")

