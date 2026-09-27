#!/usr/bin/env python3
# Copyright (C) 2026 GrizzlyOne95
#
# This file is part of Extra Utilities.
#
# Extra Utilities is free software: you can redistribute it and/or modify it
# under the terms of the GNU Lesser General Public License as published by the
# Free Software Foundation, either version 3 of the License, or (at your
# option) any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
# details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.
from __future__ import annotations

import contextlib
import io
import json
import struct
import tempfile
import unittest
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import qualify_bzr_build as q


def build_pe(text: bytes, image_base: int = 0x00400000, text_rva: int = 0x1000, steamstub: bool = False) -> bytes:
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
        2 if steamstub else 1,
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
    if steamstub:
        # SteamStub's loader section; its raw data is irrelevant here.
        bind = section + 40
        data[bind:bind + 8] = b".bind\0\0\0"
        struct.pack_into("<IIII", data, bind + 8, 0x1000, text_rva + 0x10000, 0, 0)
        struct.pack_into("<I", data, bind + 36, 0x60000020)

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


def write_exe(root: Path, text: bytes, steamstub: bool = False) -> Path:
    exe_path = root / "bzr.exe"
    exe_path.write_bytes(build_pe(text, steamstub=steamstub))
    return exe_path


def catalog_with(entries: dict) -> dict:
    return {"addresses": {"Test": entries}}


class CatalogModeTests(unittest.TestCase):
    CODE = {"address": "0x00401010", "pattern": "AA BB CC DD", "pattern_kind": "code"}
    # mov eax, [0x00402000] followed by ret
    REFERENCE = {"address": "0x00402000", "pattern": "A1 00 20 40 00 C3", "pattern_kind": "reference"}

    def qualify(self, text: bytes, entries: dict) -> dict:
        with tempfile.TemporaryDirectory() as td:
            image = q.PEImage.load(write_exe(Path(td), text))
            return q.qualify_catalog(image, catalog_with(entries))

    def state(self, text: bytes, entry: dict) -> dict:
        return self.qualify(text, {"Entry": entry})["entries"][0]

    def test_code_pattern_states(self):
        text = bytearray(b"\x90" * 0x100)
        text[0x10:0x14] = b"\xAA\xBB\xCC\xDD"
        self.assertEqual(self.state(bytes(text), self.CODE)["state"], "MATCH")

        moved = bytearray(b"\x90" * 0x100)
        moved[0x40:0x44] = b"\xAA\xBB\xCC\xDD"
        result = self.state(bytes(moved), self.CODE)
        self.assertEqual(result["state"], "RELOCATED")
        self.assertEqual(result["relocated_to"], 0x00401040)

        self.assertEqual(self.state(b"\x90" * 0x100, self.CODE)["state"], "MISSING")

        doubled = bytearray(text)
        doubled[0x40:0x44] = b"\xAA\xBB\xCC\xDD"
        self.assertEqual(self.state(bytes(doubled), self.CODE)["state"], "AMBIGUOUS")

    def test_reference_pattern_states(self):
        text = bytearray(b"\x90" * 0x100)
        text[0x20:0x26] = b"\xA1\x00\x20\x40\x00\xC3"
        result = self.state(bytes(text), self.REFERENCE)
        self.assertEqual(result["state"], "MATCH")
        self.assertEqual(result["matches"], [0x00401020])

        # Same instruction, but the global moved to 0x00402040.
        moved = bytearray(b"\x90" * 0x100)
        moved[0x20:0x26] = b"\xA1\x40\x20\x40\x00\xC3"
        result = self.state(bytes(moved), self.REFERENCE)
        self.assertEqual(result["state"], "RELOCATED")
        self.assertEqual(result["relocated_to"], 0x00402040)

        doubled = bytearray(text)
        doubled[0x60:0x66] = b"\xA1\x00\x20\x40\x00\xC3"
        self.assertEqual(self.state(bytes(doubled), self.REFERENCE)["state"], "AMBIGUOUS")
        self.assertEqual(self.state(b"\x90" * 0x100, self.REFERENCE)["state"], "MISSING")

    def test_reference_pattern_must_embed_address(self):
        entry = {"address": "0x00402000", "pattern": "A1 ?? ?? ?? ?? C3", "pattern_kind": "reference"}
        with self.assertRaises(ValueError):
            self.state(b"\x90" * 0x100, entry)

    def test_pattern_kind_required(self):
        with self.assertRaises(ValueError):
            self.state(b"\x90" * 0x100, {"address": "0x00401010", "pattern": "AA BB"})

    def test_address_only_entries_are_listed_not_checked(self):
        text = bytearray(b"\x90" * 0x100)
        text[0x10:0x14] = b"\xAA\xBB\xCC\xDD"
        result = self.qualify(bytes(text), {
            "Entry": self.CODE,
            "NoSig": {"address": "0x00403000", "pattern": None},
        })
        self.assertEqual(result["status"], "CATALOG_MATCH")
        self.assertEqual(result["address_only"], ["Test.NoSig"])

    def test_steamstub_is_reported_instead_of_scanned(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            text = bytearray(b"\x90" * 0x100)
            text[0x10:0x14] = b"\xAA\xBB\xCC\xDD"
            exe_path = write_exe(root, bytes(text), steamstub=True)
            self.assertTrue(q.PEImage.load(exe_path).steamstub_packed)
            (root / "catalog.json").write_text(json.dumps(catalog_with({"Entry": self.CODE})), encoding="utf-8")
            (root / "profile.json").write_text(json.dumps({
                "schema_version": 1, "profile_id": "test", "game": "test", "version": "1",
                "executable": "bzr.exe", "architecture": "x86", "image_base": "0x00400000",
                "catalog": "catalog.json", "anchors": [],
            }), encoding="utf-8")
            out, err = io.StringIO(), io.StringIO()
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                code = q.main([str(exe_path), "--catalog", "--profile", str(root / "profile.json")])
            self.assertEqual(code, 3)
            self.assertIn("SteamStub", err.getvalue())
            self.assertNotIn("MISSING", out.getvalue())

            plain = write_exe(root, bytes(text))
            self.assertFalse(q.PEImage.load(plain).steamstub_packed)
            with contextlib.redirect_stdout(io.StringIO()):
                code = q.main([str(plain), "--catalog", "--profile", str(root / "profile.json")])
            self.assertEqual(code, 0)


if __name__ == "__main__":
    unittest.main()
