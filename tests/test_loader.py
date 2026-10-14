import tempfile
import unittest
from pathlib import Path

from tools.load_fx2 import LoadError, chunks, parse_hex


def record(address, kind, payload=b""):
    data = bytes([len(payload), address >> 8, address & 0xff, kind]) + payload
    return ":" + (data + bytes([(-sum(data)) & 0xff])).hex().upper()


class HexTests(unittest.TestCase):
    def parse(self, lines):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "image.ihx"
            path.write_text("\n".join(lines) + "\n", encoding="ascii")
            return parse_hex(path)

    def test_sparse_image_chunks_and_extended_zero_address(self):
        image = self.parse([record(0, 4, b"\x00\x00"), record(0, 0, b"\x12\x34"),
                            record(0x100, 0, b"\x56"), record(0, 1)])
        self.assertEqual(list(chunks(image)), [(0, b"\x12\x34"), (0x100, b"\x56")])

    def test_rejects_bad_checksum_and_out_of_range_address(self):
        with self.assertRaisesRegex(LoadError, "checksum"):
            self.parse([record(0, 0, b"\x12")[:-2] + "00", record(0, 1)])
        with self.assertRaisesRegex(LoadError, "outside firmware area"):
            self.parse([record(0x3e00, 0, b"\x12"), record(0, 1)])

    def test_rejects_conflicting_overlap_and_missing_eof(self):
        with self.assertRaisesRegex(LoadError, "conflicting"):
            self.parse([record(0, 0, b"\x12"), record(0, 0, b"\x34"), record(0, 1)])
        with self.assertRaisesRegex(LoadError, "missing EOF"):
            self.parse([record(0, 0, b"\x12")])


if __name__ == "__main__":
    unittest.main()
