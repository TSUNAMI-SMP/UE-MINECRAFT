"""Bounded native world package validation shared by the editor import and fixtures.

This validates the offline file contract; it does not execute or simulate Unreal.
Cell coordinates are in 8-block cells, and source/spawn coordinates are absolute
Minecraft positions. Every scoped cell, including an empty one, must be present.
"""
from __future__ import annotations

import hashlib
import itertools
import json
import math
import pathlib
import re
import uuid
from typing import BinaryIO, Iterable

MAX_FILE_BYTES = 512 * 1024 * 1024
MAX_LINE_BYTES = 2 * 1024 * 1024
MAX_ROWS = 2_097_152
IDENTIFIER = re.compile(r"[a-z0-9_.\-/]+:[a-z0-9_.\-/]+\Z")
STATE_KEY = re.compile(r"[a-z0-9_=,.\-]*\Z")
HASH = re.compile(r"[a-f0-9]{64}\Z")


class NativeWorldError(ValueError):
    pass


def _number(value, low, high, label, integer=False):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise NativeWorldError(f"{label}: expected a number")
    if not math.isfinite(value) or not low <= value <= high or (integer and value != math.floor(value)):
        raise NativeWorldError(f"{label}: outside its finite data budget")
    return int(value) if integer else value


def _vector(value, label, limit=30_000_000, integer=False):
    if not isinstance(value, list) or len(value) != 3:
        raise NativeWorldError(f"{label}: expected three coordinates")
    return [_number(n, -limit, limit, label, integer) for n in value]


def _identifier(value, label):
    if not isinstance(value, str) or len(value) > 256 or not IDENTIFIER.fullmatch(value) or ".." in value:
        raise NativeWorldError(f"{label}: invalid namespaced identifier")


def _guid(value, label):
    try:
        if not isinstance(value, str) or str(uuid.UUID(value)) != value.lower():
            raise ValueError
    except (ValueError, TypeError, AttributeError) as exc:
        raise NativeWorldError(f"{label}: expected UUID") from exc


def _inside(cell, header):
    c, r, h = header["center"], header["radius"], header["halfHeight"]
    return abs(cell[0] - c[0]) <= r and abs(cell[1] - c[1]) <= h and abs(cell[2] - c[2]) <= r


def validate_header(header):
    if not isinstance(header, dict) or header.get("type") != "native_world" or header.get("schema") != 1:
        raise NativeWorldError("Invalid native world schema")
    _guid(header.get("id"), "world id")
    _identifier(header.get("dimension"), "dimension")
    origin = _vector(header.get("origin"), "origin")
    spawn = _vector(header.get("spawn"), "spawn")
    _vector(header.get("center"), "center", 3_750_000, integer=True)
    radius = _number(header.get("radius"), 1, 12, "radius", True)
    height = _number(header.get("halfHeight"), 1, 6, "halfHeight", True)
    cells = _number(header.get("cells"), 27, 8125, "cells", True)
    if cells != (2 * radius + 1) ** 2 * (2 * height + 1):
        raise NativeWorldError("Header cell count does not match its finite scope")
    if max(abs(a - b) for a, b in zip(spawn, origin)) > 100_000 or not _inside([math.floor(n / 8) for n in spawn], header):
        raise NativeWorldError("Player spawn is outside the exported terrain")
    _number(header.get("yaw"), -1e9, 1e9, "yaw")
    _number(header.get("pitch"), -90, 90, "pitch")
    if "vanillaLight" in header:
        env = header["vanillaLight"]
        if not isinstance(env, dict):
            raise NativeWorldError("vanillaLight is not an object")
        for key in ("skyFactor", "blockFactor"):
            _number(env.get(key), 0, 4, key)
        for key in ("ambient", "gamma", "nightVision", "darkness", "darkenWorld"):
            _number(env.get(key), 0, 1, key)
        for key in ("skyColor", "ambientColor"):
            _number(env.get(key), 0, 0xFFFFFF, key, True)
        if not isinstance(env.get("hasSky"), bool):
            raise NativeWorldError("hasSky must be boolean")
    if "runtimeState" in header:
        runtime = header["runtimeState"]
        if not isinstance(runtime, dict) or len(json.dumps(runtime, separators=(",", ":"))) > 262144:
            raise NativeWorldError("Runtime state exceeds its data budget")
        if "inventory" in runtime and not isinstance(runtime["inventory"], dict):
            raise NativeWorldError("Saved inventory must be an object; defaults cannot replace corrupt inventory")
        for key in ("lighting", "flying"):
            if key in runtime and not isinstance(runtime[key], bool):
                raise NativeWorldError(f"Saved {key} must be boolean")
        if "health" in runtime:
            _number(runtime["health"], 0, 20, "saved health")
        if "perspective" in runtime:
            _number(runtime["perspective"], 0, 2, "saved perspective", True)
        if "respawn" in runtime:
            respawn = _vector(runtime["respawn"], "saved respawn")
            if max(abs(a - b) for a, b in zip(respawn, origin)) > 100_000 or not _inside([math.floor(n / 8) for n in respawn], header):
                raise NativeWorldError("Saved respawn is outside the exported terrain")
        for key, limit in (("drops", 128), ("fuses", 64), ("mobs", 128)):
            if key in runtime:
                values = runtime[key]
                if not isinstance(values, list) or len(values) > limit or any(not isinstance(value, dict) for value in values):
                    raise NativeWorldError(f"Saved {key} must contain bounded object records")
    mobs = header.get("mobs", [])
    if not isinstance(mobs, list) or len(mobs) > 512:
        raise NativeWorldError("Mob snapshot exceeds its data budget")
    ids = set()
    for mob in mobs:
        if not isinstance(mob, dict):
            raise NativeWorldError("Invalid mob snapshot")
        _guid(mob.get("id"), "mob id")
        if mob["id"] in ids:
            raise NativeWorldError("Duplicate mob id")
        ids.add(mob["id"])
        _identifier(mob.get("type"), "mob type")
        if not isinstance(mob.get("appearance"), str) or not HASH.fullmatch(mob["appearance"]):
            raise NativeWorldError("Invalid mob appearance")
        position = _vector(mob.get("position"), "mob position")
        if max(abs(a - b) for a, b in zip(position, origin)) > 100_000 or not _inside([math.floor(n / 8) for n in position], header):
            raise NativeWorldError("Mob lies outside the exported terrain")
        _number(mob.get("yaw"), -1e9, 1e9, "mob yaw")
        _number(mob.get("health"), 0.01, 10000, "mob health")
    return header


def validate_cell(cell, header):
    if not isinstance(cell, dict) or cell.get("type") != "cell":
        raise NativeWorldError("Invalid cell record")
    position = _vector(cell.get("cell"), "cell", 3_750_000, integer=True)
    if not _inside(position, header):
        raise NativeWorldError("Cell lies outside the finite scope")
    palette, blocks = cell.get("palette"), cell.get("blocks")
    if not isinstance(palette, list) or len(palette) > 512 or not isinstance(blocks, list) or len(blocks) > 512:
        raise NativeWorldError("Cell exceeds its palette or block budget")
    for entry in palette:
        if not isinstance(entry, list) or len(entry) != 5:
            raise NativeWorldError("Invalid palette descriptor")
        _identifier(entry[0], "block id")
        if not isinstance(entry[1], str) or len(entry[1]) > 1024 or not STATE_KEY.fullmatch(entry[1]):
            raise NativeWorldError("Invalid block state")
        _number(entry[2], 0, 0xFFFFFF, "block color", True)
        _number(entry[3], 0, 15, "opacity", True)
        _number(entry[4], 0, 15, "emission", True)
    seen = set()
    for row in blocks:
        if not isinstance(row, list) or len(row) != 4:
            raise NativeWorldError("Invalid block row")
        index = _number(row[0], 0, 511, "local index", True)
        if index in seen:
            raise NativeWorldError("Duplicate source voxel")
        seen.add(index)
        _number(row[1], 0, len(palette) - 1, "palette index", True)
        _number(row[2], 0, 15, "sky light", True)
        _number(row[3], 0, 15, "block light", True)
        voxel = [position[0] * 8 + (index & 7), position[1] * 8 + (index >> 6), position[2] * 8 + ((index >> 3) & 7)]
        if max(abs(n) for n in voxel) > 30_000_000 or max(abs(n + 0.5 - o) for n, o in zip(voxel, header["origin"])) > 100_000:
            raise NativeWorldError("Source voxel exceeds coordinate budget")
    if "skyTop" in cell:
        if not isinstance(cell["skyTop"], list) or len(cell["skyTop"]) != 64:
            raise NativeWorldError("Invalid sky boundary")
        for value in cell["skyTop"]:
            _number(value, 0, 15, "sky boundary", True)
    if "water" in cell:
        water = cell["water"]
        if not isinstance(water, list) or len(water) > 512:
            raise NativeWorldError("Invalid water occupancy")
        indices = [_number(value, 0, 511, "water voxel", True) for value in water]
        if len(set(indices)) != len(indices):
            raise NativeWorldError("Duplicate water voxel")
    if "biomeTints" in cell:
        tint = cell["biomeTints"]
        if not isinstance(tint, dict) or not isinstance(tint.get("palette"), list) or not 1 <= len(tint["palette"]) <= 512:
            raise NativeWorldError("Invalid biome tint palette")
        for colors in tint["palette"]:
            if not isinstance(colors, list) or len(colors) != 3:
                raise NativeWorldError("Invalid biome tint triple")
            for value in colors:
                _number(value, 0, 0xFFFFFF, "biome tint color", True)
        if not isinstance(tint.get("indices"), list) or len(tint["indices"]) != 512:
            raise NativeWorldError("Biome tint field must contain all 512 voxels")
        for value in tint["indices"]:
            _number(value, 0, len(tint["palette"]) - 1, "biome tint index", True)
    return tuple(position), len(blocks)


def _record(stream: BinaryIO):
    raw = stream.readline(MAX_LINE_BYTES + 1)
    if not raw:
        return None
    if len(raw) > MAX_LINE_BYTES or b"\0" in raw:
        raise NativeWorldError("Invalid or oversized world record")
    try:
        return json.loads(raw.decode("utf-8-sig"), parse_constant=lambda _: (_ for _ in ()).throw(ValueError("Nonfinite JSON")))
    except (ValueError, UnicodeError) as exc:
        raise NativeWorldError("Malformed JSON world record") from exc


def validate_world_file(path):
    path = pathlib.Path(path)
    if not 0 < path.stat().st_size <= MAX_FILE_BYTES:
        raise NativeWorldError("World file exceeds its 512 MiB data budget or is empty")
    with path.open("rb") as stream:
        header = validate_header(_record(stream))
        cells, rows = set(), 0
        while (record := _record(stream)) is not None:
            position, count = validate_cell(record, header)
            if position in cells:
                raise NativeWorldError("Duplicate cell")
            cells.add(position)
            rows += count
            if len(cells) > header["cells"] or rows > MAX_ROWS:
                raise NativeWorldError("World exceeds its cell or logical block budget")
        if len(cells) != header["cells"]:
            raise NativeWorldError("World is incomplete; missing cells cannot be assumed empty")
    return {"header": header, "cells": len(cells), "rows": rows}


def resolve_world_file(manifest_path, manifest):
    root = pathlib.Path(manifest_path).resolve().parent
    world = manifest.get("world", {})
    relative = world.get("file") if isinstance(world, dict) else None
    if not isinstance(relative, str) or not relative or len(relative) > 256 or "\\" in relative or ":" in relative:
        raise NativeWorldError("Invalid relative world file")
    child = pathlib.PurePosixPath(relative)
    if child.is_absolute() or ".." in child.parts:
        raise NativeWorldError("World file must stay inside its export folder")
    path = (root / relative).resolve()
    if not path.is_relative_to(root):
        raise NativeWorldError("World file resolves outside its export folder")
    expected = world.get("sha256")
    if not isinstance(expected, str) or not HASH.fullmatch(expected):
        raise NativeWorldError("World file must have a SHA-256 checksum")
    digest = hashlib.sha256()
    if not 0 < path.stat().st_size <= MAX_FILE_BYTES:
        raise NativeWorldError("World file exceeds its byte budget")
    with path.open("rb") as stream:
        for data in iter(lambda: stream.read(65536), b""):
            digest.update(data)
    if digest.hexdigest() != expected:
        raise NativeWorldError("World checksum does not match; export is incomplete or changed")
    if "bytes" in world and world["bytes"] != path.stat().st_size:
        raise NativeWorldError("World byte count does not match manifest")
    return path


def write_world_file(path, header, cells: Iterable[dict]):
    """Write/validate a new file atomically. A failed write preserves an existing file."""
    path = pathlib.Path(path)
    temporary = path.with_name(path.name + ".tmp-" + uuid.uuid4().hex)
    try:
        with temporary.open("xb") as stream:
            for record in itertools.chain((header,), cells):
                line = json.dumps(record, separators=(",", ":"), ensure_ascii=False, allow_nan=False).encode("utf-8") + b"\n"
                if len(line) > MAX_LINE_BYTES or stream.tell() + len(line) > MAX_FILE_BYTES:
                    raise NativeWorldError("World serialization exceeds byte budget")
                stream.write(line)
            stream.flush()
            import os
            os.fsync(stream.fileno())
        result = validate_world_file(temporary)
        temporary.replace(path)
        return result
    finally:
        temporary.unlink(missing_ok=True)
