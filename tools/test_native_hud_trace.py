#!/usr/bin/env python3
"""Host checks for the qualification tool's executable identity boundary."""
import json
from pathlib import Path
import struct
import tempfile
import unittest

from trace_native_hud import ASSETS, build_config, pe_fingerprints


def fixture():
    data = bytearray(0x400)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HH", data, 0x84, 0x14C, 1)
    struct.pack_into("<H", data, 0x94, 0xE0)
    struct.pack_into("<H", data, 0x98, 0x10B)
    struct.pack_into("<I", data, 0xB4, 0x400000)
    struct.pack_into("<8sIIII", data, 0x178, b".text", 0x100, 0x1000, 0x100, 0x200)
    struct.pack_into("<I", data, 0x19C, 0x60000020)
    data[0x210:0x230] = bytes(range(1, 33))
    return data


class PeBoundaryTests(unittest.TestCase):
    def setUp(self):
        self.profile = {"image_base": 0x400000, "targets": {"render": 0x1010}}

    def test_raw_mapping_uses_section_offset(self):
        self.assertEqual(pe_fingerprints(fixture(), self.profile)["render"]["bytes"], list(range(1, 33)))

    def test_rejects_truncated_headers_and_target_bytes(self):
        for size in (0, 2, 64, 0x180, 0x220):
            with self.subTest(size=size), self.assertRaises(ValueError):
                pe_fingerprints(fixture()[:size], self.profile)

    def test_rejects_wrong_architecture_and_base(self):
        for offset, fmt, value in ((0x84, "<H", 0x8664), (0x98, "<H", 0x20B), (0xB4, "<I", 0x500000)):
            data = fixture()
            struct.pack_into(fmt, data, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                pe_fingerprints(data, self.profile)

    def test_rejects_non_executable_section_and_unbacked_rva(self):
        data = fixture()
        struct.pack_into("<I", data, 0x19C, 0x40000040)
        with self.assertRaises(ValueError):
            pe_fingerprints(data, self.profile)
        for rva in (0x100, 0x10F0, 0x2000):
            with self.subTest(rva=rva), self.assertRaises(ValueError):
                pe_fingerprints(fixture(), {**self.profile, "targets": {"render": rva}})

    def test_hash_mismatch_fails_before_frida_import(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "battlezone98redux.exe"
            executable.write_bytes(fixture())
            with self.assertRaisesRegex(ValueError, "hash"):
                build_config(executable, 1, 10)

    def test_profile_points_to_traced_corpus_entries(self):
        profile = json.loads((ASSETS / "gog_2_2_301.json").read_text())
        self.assertEqual({name: value + profile["image_base"] for name, value in profile["targets"].items()}, {
            "render": 0x005DC300, "sprite": 0x0068CA30, "fill": 0x0068AF70, "text": 0x00689D10})
        self.assertEqual(profile["viewport_rva"] + profile["image_base"], 0x02CECEE0)


if __name__ == "__main__":
    unittest.main()
