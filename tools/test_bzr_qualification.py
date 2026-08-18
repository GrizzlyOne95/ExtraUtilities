#!/usr/bin/env python3
from __future__ import annotations

import json
import struct
import tempfile
import unittest
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import qualify_bzr_build as q


def build_pe(text: bytes, image_base: int = 0x00400000, text_rva: int = 0x1000) -> bytes:
    file_alignment = 0x200
    headers_size = 0x200
    raw_size = ((len(text) + file_alignment - 1) // file_alignment) * file_alignment
    data = bytearray(headers_size + raw_size)

    data[0:2] = b"MZ"
    pe_offset = 0x80
    struct.pack_into("<I", data, 0x3C, pe_offset)
    data[pe_offset:pe_offset + 4] = b"PE\0\0"

    file_header = pe_offset + 4
    struct.pack_into(
        "<HHIIIHH",
        data,
        file_header,
        q.IMAGE_FILE_MACHINE_I386,
        1,
        0x65000000,
        0,
        0,
        0xE0,
        0x010F,
    )

    optional = pe_offset + 24
    struct.pack_into("<H", data, optional, q.IMAGE_NT_OPTIONAL_HDR32_MAGIC)
    struct.pack_into("<I", data, optional + 16, text_rva)
    struct.pack_into("<I", data, optional + 20, text_rva)
    struct.pack_into("<I", data, optional + 24, text_rva + raw_size)
    struct.pack_into("<I", data, optional + 28, image_base)
    struct.pack_into("<I", data, optional + 32, 0x1000)
    struct.pack_into("<I", data, optional + 36, file_alignment)
    struct.pack_into("<I", data, optional + 56, 0x3000)
    struct.pack_into("<I", data, optional + 60, headers_size)
    struct.pack_into("<H", data, optional + 68, 3)
    struct.pack_into("<I", data, optional + 92, 16)

    section = optional + 0xE0
    data[section:section + 8] = b".text\0\0\0"
    struct.pack_into("<IIII", data, section + 8, len(text), text_rva, raw_size, headers_size)
    struct.pack_into("<I", data, section + 36, 0x60000020)

    data[headers_size:headers_size + len(text)] = text
    return bytes(data)


class QualificationTests(unittest.TestCase):
    def test_parse_ida_pattern(self):
        self.assertEqual(q.parse_ida_pattern("55 8B ? ?? C3"), [0x55, 0x8B, None, None, 0xC3])

    def test_expected_va_match_and_relocation(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            profile_path = root / "profile.json"
            catalog_path = root / "catalog.json"
            exe_path = root / "bzr.exe"

            catalog_path.write_text(json.dumps({
                "addresses": {
                    "Test": {
                        "Anchor": {
                            "address": "0x00401010",
                            "pattern": "AA BB CC"
                        }
                    }
                }
            }), encoding="utf-8")
            profile_path.write_text(json.dumps({
                "schema_version": 1,
                "profile_id": "test",
                "game": "test",
                "version": "1",
                "executable": "bzr.exe",
                "architecture": "x86",
                "image_base": "0x00400000",
                "catalog": "catalog.json",
                "anchors": [{
                    "name": "Test.Anchor",
                    "source": "catalog",
                    "catalog_path": "Test.Anchor",
                    "match": "expected_va",
                    "expected_va": "0x00401010",
                    "required": True,
                    "runtime_gate": True
                }]
            }), encoding="utf-8")

            text = bytearray(b"\x90" * 0x100)
            text[0x10:0x13] = b"\xAA\xBB\xCC"
            exe_path.write_bytes(build_pe(bytes(text)))

            profile, catalog = q.load_profile(profile_path)
            image = q.PEImage.load(exe_path)
            result = q.qualify(image, profile, catalog)
            self.assertEqual(result["status"], "SUPPORTED_PROFILE_MATCH")
            self.assertEqual(result["anchors"][0]["state"], "MATCH")

            text = bytearray(b"\x90" * 0x100)
            text[0x20:0x23] = b"\xAA\xBB\xCC"
            exe_path.write_bytes(build_pe(bytes(text)))
            result = q.qualify(q.PEImage.load(exe_path), profile, catalog)
            self.assertEqual(result["status"], "UNKNOWN_OR_CHANGED_BUILD")
            self.assertEqual(result["anchors"][0]["state"], "RELOCATED")

    def test_unique_signature_reports_ambiguity(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            catalog_path = root / "catalog.json"
            profile_path = root / "profile.json"
            exe_path = root / "bzr.exe"
            catalog_path.write_text(json.dumps({"addresses": {}}), encoding="utf-8")
            profile_path.write_text(json.dumps({
                "schema_version": 1,
                "profile_id": "test",
                "game": "test",
                "version": "1",
                "executable": "bzr.exe",
                "architecture": "x86",
                "image_base": "0x00400000",
                "catalog": "catalog.json",
                "anchors": [{
                    "name": "inline",
                    "source": "inline",
                    "pattern": "DE AD BE EF",
                    "match": "unique_executable",
                    "required": True,
                    "runtime_gate": True
                }]
            }), encoding="utf-8")
            text = bytearray(b"\x90" * 0x100)
            text[0x10:0x14] = b"\xDE\xAD\xBE\xEF"
            text[0x30:0x34] = b"\xDE\xAD\xBE\xEF"
            exe_path.write_bytes(build_pe(bytes(text)))

            profile, catalog = q.load_profile(profile_path)
            result = q.qualify(q.PEImage.load(exe_path), profile, catalog)
            self.assertEqual(result["anchors"][0]["state"], "AMBIGUOUS")
            self.assertEqual(result["status"], "UNKNOWN_OR_CHANGED_BUILD")

    def test_executable_contains_allows_multiple_references(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            catalog_path = root / "catalog.json"
            profile_path = root / "profile.json"
            exe_path = root / "bzr.exe"
            catalog_path.write_text(json.dumps({"addresses": {}}), encoding="utf-8")
            profile_path.write_text(json.dumps({
                "schema_version": 1,
                "profile_id": "test",
                "game": "test",
                "version": "1",
                "executable": "bzr.exe",
                "architecture": "x86",
                "image_base": "0x00400000",
                "catalog": "catalog.json",
                "anchors": [{
                    "name": "inline",
                    "source": "inline",
                    "pattern": "A1 11 22 33 44",
                    "match": "executable_contains",
                    "required": True,
                    "runtime_gate": True
                }]
            }), encoding="utf-8")
            text = bytearray(b"\x90" * 0x100)
            text[0x10:0x15] = b"\xA1\x11\x22\x33\x44"
            text[0x30:0x35] = b"\xA1\x11\x22\x33\x44"
            exe_path.write_bytes(build_pe(bytes(text)))

            profile, catalog = q.load_profile(profile_path)
            result = q.qualify(q.PEImage.load(exe_path), profile, catalog)
            self.assertEqual(result["anchors"][0]["state"], "MATCH")
            self.assertEqual(result["status"], "SUPPORTED_PROFILE_MATCH")


if __name__ == "__main__":
    unittest.main()
