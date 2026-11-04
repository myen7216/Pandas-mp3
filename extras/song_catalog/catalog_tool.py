#!/usr/bin/env python3
"""Inactive, read-only MP3 catalog builder. Does not upload or modify music."""
import argparse
import json
import math
from pathlib import Path
import re
import sys

SCHEMA_VERSION = 1
MAX_TRACKS = 200  # Initial design limit; validate ESP32 memory before raising.
RESERVED_INTRO_ID = 11


def read_duration_ms(path):
    try:
        from mutagen.mp3 import MP3
    except ImportError as exc:
        raise ValueError("Install the optional tool dependencies from requirements.txt first") from exc
    try:
        seconds = MP3(path).info.length
    except Exception as exc:
        raise ValueError(f"Cannot read MP3 duration: {path.name}: {exc}") from exc
    if not math.isfinite(seconds) or seconds <= 0:
        raise ValueError(f"Invalid MP3 duration: {path.name}")
    return max(1, round(seconds * 1000))


def parse_name(path):
    # Split only twice: hyphens in a title are preserved. Artist hyphens are
    # ambiguous with this naming convention; use ID3 in a future extension.
    parts = path.stem.split("-", 2)
    if len(parts) != 3 or not re.fullmatch(r"[0-9]{1,4}", parts[0]):
        raise ValueError(f"Expected number-Artist-Title.mp3: {path.name}")
    track_id = int(parts[0])
    if not 1 <= track_id <= 9999:
        raise ValueError(f"Song number must be 1..9999: {path.name}")
    if track_id == RESERVED_INTRO_ID:
        raise ValueError("Track 11 is reserved for the startup sound; use 12 for the next song")
    artist, title = (part.replace("_", " ").strip() for part in parts[1:])
    for label, value in (("artist", artist), ("title", title)):
        if not value or any(ord(char) < 32 or ord(char) == 127 for char in value):
            raise ValueError(f"Invalid {label}: {path.name}")
        if len(value.encode("utf-8")) > 120:
            raise ValueError(f"{label.title()} exceeds 120 UTF-8 bytes: {path.name}")
    return track_id, artist, title


def build_catalog(folder, duration_reader=read_duration_ms):
    folder = Path(folder)
    if not folder.is_dir():
        raise ValueError(f"Music directory not found: {folder}")
    tracks, seen = [], set()
    for path in sorted(folder.iterdir()):
        if not path.is_file() or path.name.startswith(".") or path.suffix.lower() != ".mp3":
            continue
        # The existing, plain-number startup jingle is not a playlist entry.
        if path.stem == "0011":
            continue
        track_id, artist, title = parse_name(path)
        if track_id in seen:
            raise ValueError(f"Duplicate song number {track_id}: {path.name}")
        seen.add(track_id)
        duration_ms = duration_reader(path)
        if isinstance(duration_ms, bool) or not isinstance(duration_ms, int) or duration_ms <= 0:
            raise ValueError(f"Invalid duration: {path.name}")
        tracks.append(dict(id=track_id, artist=artist, title=title,
                           duration_ms=duration_ms, source_filename=path.name,
                           playback_filename=f"{track_id:04d}-{path.stem.split('-', 1)[1]}.mp3"))
    if not tracks:
        raise ValueError("No named MP3 songs found")
    if len(tracks) > MAX_TRACKS:
        raise ValueError(f"Prototype supports at most {MAX_TRACKS} songs")
    tracks.sort(key=lambda track: track["id"])
    return dict(schema_version=SCHEMA_VERSION, reserved_intro_id=RESERVED_INTRO_ID, tracks=tracks)


def write_catalog(catalog, output, music_folder):
    output = Path(output).resolve()
    folder = Path(music_folder).resolve()
    # Never write to the input directory or any mounted drive (including SD).
    if output == folder or folder in output.parents or Path("/Volumes") in output.parents:
        raise ValueError("Save the catalog locally outside the music folder and /Volumes")
    with output.open("x", encoding="utf-8") as stream:
        json.dump(catalog, stream, ensure_ascii=False, indent=2)
        stream.write("\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("music_folder", type=Path)
    parser.add_argument("--output", type=Path, help="Optional NEW local JSON file; existing files are never overwritten")
    args = parser.parse_args()
    try:
        catalog = build_catalog(args.music_folder)
        if args.output:
            write_catalog(catalog, args.output, args.music_folder)
        else:
            print(json.dumps(catalog, ensure_ascii=False, indent=2))
        print(f"Prepared {len(catalog['tracks'])} songs; no SD writes or device upload performed.", file=sys.stderr)
    except (ValueError, OSError) as exc:
        print(f"Catalog error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
