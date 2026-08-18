#!/usr/bin/env python3
from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import os
import struct
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable, Optional

IMAGE_FILE_MACHINE_I386 = 0x014C
IMAGE_NT_OPTIONAL_HDR32_MAGIC = 0x10B
IMAGE_SCN_MEM_EXECUTE = 0x20000000


@dataclass(frozen=True)
class Section:
    name: str
    virtual_address: int
    virtual_size: int
    raw_offset: int
    raw_size: int
    characteristics: int

    @property
    def executable(self) -> bool:
        return bool(self.characteristics & IMAGE_SCN_MEM_EXECUTE)


@dataclass
class PEImage:
    path: Path
    data: bytes
    machine: int
    timestamp: int
    image_base: int
    sections: list[Section]

    @classmethod
    def load(cls, path: Path) -> "PEImage":
        data = path.read_bytes()
        if len(data) < 0x100 or data[:2] != b"MZ":
            raise ValueError("not a valid PE image (missing MZ header)")

        pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
        if pe_offset + 24 > len(data) or data[pe_offset:pe_offset + 4] != b"PE\0\0":
            raise ValueError("not a valid PE image (missing PE signature)")

        machine, section_count, timestamp, _, _, optional_size, _ = struct.unpack_from(
            "<HHIIIHH", data, pe_offset + 4
        )
        optional_offset = pe_offset + 24
        if optional_offset + optional_size > len(data):
            raise ValueError("truncated PE optional header")

        magic = struct.unpack_from("<H", data, optional_offset)[0]
        if magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC:
            raise ValueError(f"unsupported PE optional header magic 0x{magic:04X}; expected PE32/x86")

        image_base = struct.unpack_from("<I", data, optional_offset + 28)[0]
        section_offset = optional_offset + optional_size
        sections: list[Section] = []
        for index in range(section_count):
            offset = section_offset + index * 40
            if offset + 40 > len(data):
                raise ValueError("truncated PE section table")
            raw_name = data[offset:offset + 8].split(b"\0", 1)[0]
            name = raw_name.decode("ascii", errors="replace")
            virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from("<IIII", data, offset + 8)
            characteristics = struct.unpack_from("<I", data, offset + 36)[0]
            sections.append(Section(name, virtual_address, virtual_size, raw_offset, raw_size, characteristics))

        return cls(path=path, data=data, machine=machine, timestamp=timestamp, image_base=image_base, sections=sections)

    def executable_sections(self) -> Iterable[Section]:
        return (section for section in self.sections if section.executable and section.raw_size > 0)

    def rva_to_file_offset(self, rva: int) -> Optional[int]:
        for section in self.sections:
            mapped_size = max(section.virtual_size, section.raw_size)
            if section.virtual_address <= rva < section.virtual_address + mapped_size:
                delta = rva - section.virtual_address
                if delta >= section.raw_size:
                    return None
                return section.raw_offset + delta
        return rva if rva < len(self.data) else None

    def find_pattern(self, pattern: list[Optional[int]], executable_only: bool = True) -> list[int]:
        matches: list[int] = []
        sections = list(self.executable_sections()) if executable_only else self.sections
        plen = len(pattern)
        if plen == 0:
            return matches

        for section in sections:
            start = section.raw_offset
            end = min(len(self.data), start + section.raw_size)
            blob = self.data[start:end]
            if len(blob) < plen:
                continue
            for offset in range(0, len(blob) - plen + 1):
                if all(expected is None or blob[offset + i] == expected for i, expected in enumerate(pattern)):
                    matches.append(self.image_base + section.virtual_address + offset)
        return matches

    def pattern_at_va(self, va: int, pattern: list[Optional[int]]) -> bool:
        rva = va - self.image_base
        if rva < 0:
            return False
        file_offset = self.rva_to_file_offset(rva)
        if file_offset is None or file_offset + len(pattern) > len(self.data):
            return False
        blob = self.data[file_offset:file_offset + len(pattern)]
        return all(expected is None or blob[i] == expected for i, expected in enumerate(pattern))


def parse_ida_pattern(text: str) -> list[Optional[int]]:
    if not isinstance(text, str) or not text.strip():
        raise ValueError("signature pattern is empty")
    pattern: list[Optional[int]] = []
    for token in text.split():
        if token in {"?", "??"}:
            pattern.append(None)
            continue
        if len(token) != 2:
            raise ValueError(f"invalid signature token {token!r}")
        try:
            value = int(token, 16)
        except ValueError as exc:
            raise ValueError(f"invalid signature token {token!r}") from exc
        if not 0 <= value <= 0xFF:
            raise ValueError(f"signature token out of byte range: {token!r}")
        pattern.append(value)
    return pattern


def walk_catalog_entries(node: object, prefix: str = "") -> Iterable[tuple[str, dict]]:
    if not isinstance(node, dict):
        return
    if "address" in node or "rva" in node or "value" in node:
        yield prefix, node
        return
    for key, value in node.items():
        if key.startswith("_"):
            continue
        child = f"{prefix}.{key}" if prefix else key
        if isinstance(value, dict):
            yield from walk_catalog_entries(value, child)


def resolve_catalog_entry(catalog: dict, path: str) -> dict:
    node: object = catalog.get("addresses", {})
    for part in path.split("."):
        if not isinstance(node, dict) or part not in node:
            raise KeyError(f"catalog entry {path!r} not found")
        node = node[part]
    if not isinstance(node, dict):
        raise KeyError(f"catalog entry {path!r} is not an object")
    return node


def load_profile(profile_path: Path) -> tuple[dict, dict]:
    profile = json.loads(profile_path.read_text(encoding="utf-8"))
    if profile.get("schema_version") != 1:
        raise ValueError("unsupported build-profile schema")
    catalog_path = (profile_path.parent / profile["catalog"]).resolve()
    catalog = json.loads(catalog_path.read_text(encoding="utf-8"))
    return profile, catalog


def anchor_pattern(anchor: dict, catalog: dict) -> list[Optional[int]]:
    source = anchor.get("source")
    if source == "catalog":
        entry = resolve_catalog_entry(catalog, anchor["catalog_path"])
        text = entry.get("pattern")
        if not text:
            raise ValueError(f"catalog anchor {anchor['name']!r} does not have a pattern")
        return parse_ida_pattern(text)
    if source == "inline":
        return parse_ida_pattern(anchor["pattern"])
    raise ValueError(f"unsupported anchor source {source!r}")


def qualify_anchor(image: PEImage, anchor: dict, catalog: dict) -> dict:
    pattern = anchor_pattern(anchor, catalog)
    matches = image.find_pattern(pattern, executable_only=True)
    mode = anchor["match"]
    expected_va = int(anchor["expected_va"], 0) if anchor.get("expected_va") else None

    if mode == "expected_va":
        if expected_va is None:
            raise ValueError(f"anchor {anchor['name']!r} requires expected_va")
        if image.pattern_at_va(expected_va, pattern):
            state = "MATCH"
        elif len(matches) == 1:
            state = "RELOCATED"
        elif len(matches) == 0:
            state = "MISSING"
        else:
            state = "AMBIGUOUS"
    elif mode == "executable_contains":
        state = "MATCH" if matches else "MISSING"
    elif mode == "unique_executable":
        if len(matches) == 1:
            state = "MATCH"
        elif len(matches) == 0:
            state = "MISSING"
        else:
            state = "AMBIGUOUS"
    else:
        raise ValueError(f"unsupported anchor match mode {mode!r}")

    return {
        "name": anchor["name"],
        "required": bool(anchor.get("required", False)),
        "runtime_gate": bool(anchor.get("runtime_gate", False)),
        "mode": mode,
        "state": state,
        "expected_va": expected_va,
        "matches": matches,
        "reason": anchor.get("reason", ""),
        "source_file": anchor.get("source_file"),
    }


def get_windows_file_version(path: Path) -> Optional[str]:
    if os.name != "nt":
        return None
    try:
        version = ctypes.windll.version
        dummy = ctypes.c_ulong(0)
        size = version.GetFileVersionInfoSizeW(str(path), ctypes.byref(dummy))
        if not size:
            return None
        buffer = ctypes.create_string_buffer(size)
        if not version.GetFileVersionInfoW(str(path), 0, size, buffer):
            return None

        value_ptr = ctypes.c_void_p()
        value_len = ctypes.c_uint()
        if not version.VerQueryValueW(buffer, "\\", ctypes.byref(value_ptr), ctypes.byref(value_len)):
            return None

        class VS_FIXEDFILEINFO(ctypes.Structure):
            _fields_ = [
                ("dwSignature", ctypes.c_uint32),
                ("dwStrucVersion", ctypes.c_uint32),
                ("dwFileVersionMS", ctypes.c_uint32),
                ("dwFileVersionLS", ctypes.c_uint32),
                ("dwProductVersionMS", ctypes.c_uint32),
                ("dwProductVersionLS", ctypes.c_uint32),
                ("dwFileFlagsMask", ctypes.c_uint32),
                ("dwFileFlags", ctypes.c_uint32),
                ("dwFileOS", ctypes.c_uint32),
                ("dwFileType", ctypes.c_uint32),
                ("dwFileSubtype", ctypes.c_uint32),
                ("dwFileDateMS", ctypes.c_uint32),
                ("dwFileDateLS", ctypes.c_uint32),
            ]

        info = ctypes.cast(value_ptr, ctypes.POINTER(VS_FIXEDFILEINFO)).contents
        parts = [
            info.dwFileVersionMS >> 16,
            info.dwFileVersionMS & 0xFFFF,
            info.dwFileVersionLS >> 16,
            info.dwFileVersionLS & 0xFFFF,
        ]
        while len(parts) > 3 and parts[-1] == 0:
            parts.pop()
        return ".".join(str(part) for part in parts)
    except Exception:
        return None


def qualify(image: PEImage, profile: dict, catalog: dict) -> dict:
    anchor_results = [qualify_anchor(image, anchor, catalog) for anchor in profile["anchors"]]

    catalog_entries = list(walk_catalog_entries(catalog.get("addresses", {})))
    bzr_address_entries = [
        (name, entry) for name, entry in catalog_entries
        if isinstance(entry.get("address"), str)
    ]
    pattern_entries = [
        (name, entry) for name, entry in bzr_address_entries
        if isinstance(entry.get("pattern"), str) and entry["pattern"].strip()
    ]

    required_failures = [
        result for result in anchor_results
        if result["required"] and result["state"] != "MATCH"
    ]
    architecture_ok = image.machine == IMAGE_FILE_MACHINE_I386
    profile_image_base = int(profile["image_base"], 0)
    image_base_ok = image.image_base == profile_image_base

    status = "SUPPORTED_PROFILE_MATCH" if architecture_ok and image_base_ok and not required_failures else "UNKNOWN_OR_CHANGED_BUILD"

    timestamp_text = datetime.fromtimestamp(image.timestamp, tz=timezone.utc).isoformat() if image.timestamp else None
    digest = hashlib.sha256(image.data).hexdigest()

    return {
        "status": status,
        "profile_id": profile["profile_id"],
        "profile_version": profile["version"],
        "file_version": get_windows_file_version(image.path),
        "path": str(image.path),
        "size": len(image.data),
        "sha256": digest,
        "machine": f"0x{image.machine:04X}",
        "architecture_ok": architecture_ok,
        "image_base": f"0x{image.image_base:08X}",
        "image_base_ok": image_base_ok,
        "pe_timestamp": image.timestamp,
        "pe_timestamp_utc": timestamp_text,
        "catalog": {
            "address_entries": len(bzr_address_entries),
            "pattern_entries": len(pattern_entries),
            "address_only_entries": len(bzr_address_entries) - len(pattern_entries),
        },
        "anchors": anchor_results,
    }


def format_va(value: Optional[int]) -> str:
    return "-" if value is None else f"0x{value:08X}"


def render_report(result: dict) -> str:
    lines = [
        "EXU / Battlezone 98 Redux Build Qualification",
        "=" * 45,
        f"Executable: {result['path']}",
        f"Size: {result['size']} bytes",
        f"SHA-256: {result['sha256']}",
        f"PE machine: {result['machine']} ({'x86 OK' if result['architecture_ok'] else 'UNEXPECTED'})",
        f"Image base: {result['image_base']} ({'OK' if result['image_base_ok'] else 'UNEXPECTED'})",
        f"PE timestamp: {result['pe_timestamp_utc'] or result['pe_timestamp']}",
        f"Windows file version: {result['file_version'] or 'unavailable'}",
        "",
        f"Profile: {result['profile_id']} (BZR {result['profile_version']})",
        f"Result: {result['status']}",
        "",
        "Anchor results",
        "--------------",
    ]

    for anchor in result["anchors"]:
        marker = {"MATCH": "OK", "RELOCATED": "MOVED", "MISSING": "MISS", "AMBIGUOUS": "AMBIG"}[anchor["state"]]
        requirement = "required" if anchor["required"] else "diagnostic"
        matches = ", ".join(format_va(value) for value in anchor["matches"][:6])
        if len(anchor["matches"]) > 6:
            matches += f", ... (+{len(anchor['matches']) - 6})"
        if not matches:
            matches = "-"
        expected = format_va(anchor["expected_va"])
        lines.append(
            f"[{marker:5}] {anchor['name']} | {requirement} | mode={anchor['mode']} | "
            f"expected={expected} | matches={matches}"
        )

    catalog = result["catalog"]
    lines.extend([
        "",
        "Catalog coverage",
        "----------------",
        f"BZR address entries: {catalog['address_entries']}",
        f"Entries with signatures: {catalog['pattern_entries']}",
        f"Address-only entries: {catalog['address_only_entries']}",
        "",
    ])

    if result["status"] == "SUPPORTED_PROFILE_MATCH":
        lines.append("Conclusion: all required anchors match the supported BZR profile.")
        lines.append("This is a qualification aid, not a substitute for an in-game EXU smoke test.")
    else:
        lines.append("Conclusion: treat this executable as unsupported until changed anchors are reviewed.")
        lines.append("Do not bypass EXU's runtime build gate merely because most diagnostic signatures still match.")

    return "\n".join(lines) + "\n"


def default_profile_path() -> Path:
    return Path(__file__).resolve().parents[1] / "profiles" / "bzr_2.2.301.json"


def main(argv: Optional[list[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Qualify a Battlezone 98 Redux executable against EXU build profiles.")
    parser.add_argument("executable", type=Path, help="Path to bzr.exe / Battlezone98Redux.exe")
    parser.add_argument("--profile", type=Path, default=default_profile_path(), help="Build-profile JSON to use")
    parser.add_argument("--write-report", action="store_true", help="Write a shareable text report in the current working directory")
    parser.add_argument("--output", type=Path, help="Explicit text report path")
    parser.add_argument("--json-output", type=Path, help="Optional machine-readable JSON result path")
    args = parser.parse_args(argv)

    try:
        profile, catalog = load_profile(args.profile.resolve())
        image = PEImage.load(args.executable.resolve())
        result = qualify(image, profile, catalog)
        report = render_report(result)
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as exc:
        print(f"qualification failed: {exc}", file=sys.stderr)
        return 2

    print(report, end="")

    output = args.output
    if args.write_report and output is None:
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        output = Path.cwd() / f"EXU_BZR_Compatibility_{stamp}.txt"
    if output is not None:
        output.write_text(report, encoding="utf-8")
        print(f"Report written to: {output}")

    if args.json_output is not None:
        args.json_output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(f"JSON written to: {args.json_output}")

    return 0 if result["status"] == "SUPPORTED_PROFILE_MATCH" else 1


if __name__ == "__main__":
    raise SystemExit(main())
