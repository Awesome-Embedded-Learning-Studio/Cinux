#!/usr/bin/env python3
"""Assemble a disk image from a one-image-per-file layout table.

Every image owns one JSON file under scripts/meta/ (the file name is the
image's name); this script only executes the file it is handed. CMake
targets call in for the x86_64 boot image, the Buildroot lane calls in
for the armv7 SD card, and both get byte-identical results to the
hand-rolled dd sequences this replaces.

Exit codes: 0 on success, 1 on any table or input violation, with the
offending entry named and the numbers that failed spelled out.
"""
import argparse
import json
import pathlib
import sys

KINDS = ("payload", "marker", "mbr")


def fail(message: str) -> "None":
    print(f"[layout] error: {message}", file=sys.stderr, flush=True)
    sys.exit(1)


def load_table(path: pathlib.Path) -> "tuple[str, dict]":
    try:
        table = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"cannot read layout table {path}: {exc}")
    if not isinstance(table, dict):
        fail(f"layout table {path} is not a JSON object")
    return path.stem, table


def check_common(label: str, image: dict) -> int:
    sector_size = image.get("sector_size", 512)
    if not isinstance(sector_size, int) or sector_size <= 0:
        fail(f"{label}: bad sector_size {sector_size!r}")
    size_bytes = image.get("size_bytes")
    if not isinstance(size_bytes, int) and size_bytes != "auto":
        fail(f"{label}: size_bytes must be a positive byte count or 'auto'")
    entries = image.get("entries")
    if not isinstance(entries, list) or not entries:
        fail(f"{label}: no entries")
    seen = set()
    for entry in entries:
        name = entry.get("name")
        if not name:
            fail(f"{label}: an entry has no name")
        if name in seen:
            fail(f"{label}: entry name '{name}' appears twice")
        seen.add(name)
        kind = entry.get("kind")
        if kind not in KINDS:
            fail(f"{label}/{name}: unknown kind {kind!r} "
                 f"(known: {', '.join(KINDS)})")
    return sector_size


def resolve_size(image: dict, sector_size: int, sources: dict,
                 label: str) -> int:
    """Fixed count, or 'auto': the image ends one sector past its
    farthest payload or marker, so any volume length bakes a whole
    card without hand-tuned totals."""
    size_bytes = image.get("size_bytes")
    if isinstance(size_bytes, int):
        if size_bytes <= 0:
            fail(f"{label}: bad size_bytes {size_bytes!r}")
        if size_bytes % sector_size:
            fail(f"{label}: size_bytes {size_bytes} is not a whole number "
                 f"of {sector_size}-byte sectors")
        return size_bytes
    end = 0
    for entry in image["entries"]:
        if entry["kind"] == "mbr":
            continue  # the table stamps sector zero, which payloads cover
        name = f"{label}/{entry['name']}"
        offset = entry_offset(entry, sector_size, name)
        if entry["kind"] == "payload":
            source = entry.get("source")
            if not source or source not in sources:
                fail(f"{name}: source '{source}' was not provided")
            path = pathlib.Path(sources[source])
            if not path.is_file():
                fail(f"{name}: source file {path} does not exist")
            length = path.stat().st_size
        elif entry["kind"] == "marker":
            length = len(hex_bytes(name, entry.get("hex", "")))
        else:
            length = 0
        end = max(end, offset + length)
    if end <= 0:
        fail(f"{label}: size_bytes 'auto' found nothing to size from")
    return (end + sector_size - 1) // sector_size * sector_size


def entry_offset(entry: dict, sector_size: int, name: str) -> int:
    at_lba, at_byte = entry.get("at_lba"), entry.get("at_byte")
    if (at_lba is None) == (at_byte is None):
        fail(f"{name}: exactly one of at_lba and at_byte is required")
    if at_lba is not None:
        if not isinstance(at_lba, int) or at_lba < 0:
            fail(f"{name}: bad at_lba {at_lba!r}")
        return at_lba * sector_size
    if not isinstance(at_byte, int) or at_byte < 0:
        fail(f"{name}: bad at_byte {at_byte!r}")
    return at_byte


def hex_bytes(name: str, value: str) -> bytes:
    try:
        raw = bytes.fromhex(value)
    except ValueError:
        fail(f"{name}: hex {value!r} is not valid hex")
    if not raw:
        fail(f"{name}: hex {value!r} decodes to zero bytes")
    return raw


def apply_payload(entry: dict, name: str, sector_size: int, size_bytes: int,
                  sources: dict, disk) -> tuple:
    source = entry.get("source")
    if not source or source not in sources:
        fail(f"{name}: source '{source}' was not provided on the command line")
    path = pathlib.Path(sources[source])
    if not path.is_file():
        fail(f"{name}: source file {path} does not exist")
    offset = entry_offset(entry, sector_size, name)
    file_size = path.stat().st_size
    end = offset + file_size
    if end > size_bytes:
        fail(f"{name}: {file_size} bytes at offset {offset} run past the "
             f"{size_bytes}-byte image")
    capacity = entry.get("capacity_sectors")
    if capacity is not None:
        if not isinstance(capacity, int) or capacity <= 0:
            fail(f"{name}: bad capacity_sectors {capacity!r}")
        limit = offset + capacity * sector_size
        if end > limit:
            fail(f"{name}: {file_size} bytes need "
                 f"{(file_size + sector_size - 1) // sector_size} sectors "
                 f"but capacity_sectors is {capacity}")
    disk.seek(offset)
    disk.write(path.read_bytes())
    return offset, file_size


def apply_marker(entry: dict, name: str, sector_size: int, size_bytes: int,
                 disk) -> tuple:
    raw = hex_bytes(name, entry.get("hex", ""))
    offset = entry_offset(entry, sector_size, name)
    if offset + len(raw) > size_bytes:
        fail(f"{name}: marker runs past the {size_bytes}-byte image")
    disk.seek(offset)
    disk.write(raw)
    return offset, len(raw)


def apply_mbr(entry: dict, name: str, size_bytes: int, disk) -> tuple:
    partition = entry.get("partition")
    if not isinstance(partition, dict):
        fail(f"{name}: the mbr kind needs a partition object")
    index = partition.get("index", 0)
    if not isinstance(index, int) or not 0 <= index <= 3:
        fail(f"{name}: partition index {index!r} is outside 0..3")
    start_lba = partition.get("start_lba")
    if not isinstance(start_lba, int) or start_lba <= 0:
        fail(f"{name}: bad partition start_lba {start_lba!r}")
    total_sectors = size_bytes // 512
    if partition.get("size") == "fill":
        sector_count = total_sectors - start_lba
        if sector_count <= 0:
            fail(f"{name}: start_lba {start_lba} leaves no room in "
                 f"{total_sectors} sectors")
    elif isinstance(partition.get("size"), int) and partition["size"] > 0:
        sector_count = partition["size"]
    else:
        fail(f"{name}: partition size must be a positive sector count or "
             f"'fill'")
    if start_lba + sector_count > total_sectors:
        fail(f"{name}: partition {start_lba}+{sector_count} sectors exceeds "
             f"the {total_sectors}-sector image")
    type_byte = hex_bytes(name, partition.get("type_hex", ""))
    if len(type_byte) != 1:
        fail(f"{name}: type_hex must be exactly one byte")
    entry_base = 446 + 16 * index
    disk.seek(entry_base + 4)
    disk.write(type_byte)
    disk.seek(entry_base + 8)
    disk.write(start_lba.to_bytes(4, "little"))
    disk.seek(entry_base + 12)
    disk.write(sector_count.to_bytes(4, "little"))
    disk.seek(510)
    disk.write(b"\x55\xAA")
    return entry_base, 16


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Assemble a disk image from its layout table.")
    parser.add_argument("--layout", type=pathlib.Path, required=True,
                        help="layout table: one JSON file per image")
    parser.add_argument("--output", type=pathlib.Path,
                        help="image file to write (skipped with --plan)")
    parser.add_argument("--source", action="append", default=[],
                        metavar="NAME=PATH",
                        help="payload source, repeatable")
    parser.add_argument("--plan", action="store_true",
                        help="print the plan without writing")
    args = parser.parse_args()

    label, image = load_table(args.layout)
    sector_size = check_common(label, image)
    sources = {}
    for pair in args.source:
        if "=" not in pair:
            fail(f"--source {pair!r} is not NAME=PATH")
        key, _, value = pair.partition("=")
        if key in sources:
            fail(f"--source {key} given twice")
        sources[key] = value
    size_bytes = resolve_size(image, sector_size, sources, label)

    print(f"[layout] {args.layout.name}: {len(image['entries'])} entries, "
          f"{size_bytes} bytes, format {image.get('format', 'raw')}",
          flush=True)
    if args.plan:
        for entry in image["entries"]:
            print(f"[layout]   {entry['kind']:7s} {entry['name']}", flush=True)
        return
    if args.output is None:
        fail("writing an image needs --output")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("wb") as disk:
        disk.truncate(size_bytes)
        for entry in image["entries"]:
            name = f"{label}/{entry['name']}"
            if entry["kind"] == "payload":
                offset, length = apply_payload(entry, name, sector_size,
                                               size_bytes, sources, disk)
            elif entry["kind"] == "marker":
                offset, length = apply_marker(entry, name, sector_size,
                                              size_bytes, disk)
            else:
                offset, length = apply_mbr(entry, name, size_bytes, disk)
            print(f"[layout]   {entry['name']:12s} kind {entry['kind']:7s} "
                  f"at byte {offset:8d}, {length} bytes", flush=True)
            disk.flush()
    print(f"[layout] {args.output}: assembled", flush=True)


if __name__ == "__main__":
    main()
