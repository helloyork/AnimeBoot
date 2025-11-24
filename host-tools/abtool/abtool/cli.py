from __future__ import annotations

import argparse
import logging
from pathlib import Path

from .anim_package import build_package
from .frames import DecodedFrame, export_frames_to_bmp, load_media_frames, resize_frame
from .manifest import FrameEntry, build_manifest_from_frames, load_manifest, save_manifest
from .preview import PreviewPlayer

LOG = logging.getLogger("abtool")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="abtool", description="AnimeBoot asset toolkit")
    parser.add_argument("--verbose", action="store_true", help="Enable debug logging")

    subparsers = parser.add_subparsers(dest="command", required=True)

    extract_parser = subparsers.add_parser("extract", help="Convert media into frame sequence")
    extract_parser.add_argument("input", type=Path)
    extract_parser.add_argument("output", type=Path)
    extract_parser.add_argument("--width", type=int, default=640)
    extract_parser.add_argument("--height", type=int, default=360)
    extract_parser.add_argument("--fps", type=int, default=24)
    extract_parser.add_argument("--scaling", choices=["letterbox", "fill", "center"], default="letterbox")
    extract_parser.add_argument("--background", default="#000000")
    extract_parser.add_argument("--manifest", type=Path, default=None)
    extract_parser.add_argument("--prefix", default="frame")

    pack_parser = subparsers.add_parser("pack", help="Pack manifest and frames into .anim container")
    pack_parser.add_argument("manifest", type=Path)
    pack_parser.add_argument("output", type=Path)
    pack_parser.add_argument("--frames-root", type=Path, default=None, help="Override frame root directory")

    preview_parser = subparsers.add_parser("preview", help="Preview .anim in a window")
    preview_parser.add_argument("package", type=Path)

    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO)

    if args.command == "extract":
        command_extract(args)
    elif args.command == "pack":
        command_pack(args)
    elif args.command == "preview":
        command_preview(args)
    else:
        parser.error("Unknown command")
    return 0


def command_extract(args: argparse.Namespace) -> None:
    frames = load_media_frames(args.input, args.fps)
    processed: list[DecodedFrame] = []
    for frame in frames:
        resized = resize_frame(frame.image, args.width, args.height, args.scaling, args.background)
        processed.append(DecodedFrame(image=resized, duration_us=frame.duration_us))
    frame_paths = export_frames_to_bmp(processed, args.output, args.prefix)
    manifest_frames = [
        FrameEntry(path=Path(path.name), duration_us=frame.duration_us)
        for path, frame in zip(frame_paths, processed, strict=False)
    ]
    manifest = build_manifest_from_frames(
        manifest_frames,
        width=args.width,
        height=args.height,
        fps=args.fps,
        scaling=args.scaling,
        background=args.background,
    )
    manifest_path = args.manifest or (args.output / "sequence.anim.json")
    save_manifest(manifest_path, manifest)
    LOG.info("Exported %d frames to %s", len(frame_paths), args.output)
    LOG.info("Manifest written to %s", manifest_path)


def command_pack(args: argparse.Namespace) -> None:
    manifest_path: Path = args.manifest
    manifest = load_manifest(manifest_path)
    root_dir = args.frames_root or manifest_path.parent
    output_path = args.output
    build_package(manifest, root_dir, output_path)
    LOG.info("Package written to %s", output_path)


def command_preview(args: argparse.Namespace) -> None:
    player = PreviewPlayer(args.package)
    player.run()

