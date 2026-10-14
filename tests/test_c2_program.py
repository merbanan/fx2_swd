import unittest

from tools.c2_program import PAGE_SIZE, program_pages


class FakeProbe:
    def __init__(self):
        self.flash = bytearray([0xff] * (15 * PAGE_SIZE))
        self.erased = []

    def read(self, address, count):
        return bytes(self.flash[address:address + count])

    def erase_page(self, page):
        self.erased.append(page)
        self.flash[page * PAGE_SIZE:(page + 1) * PAGE_SIZE] = b"\xff" * PAGE_SIZE

    def write(self, address, data):
        self.flash[address:address + len(data)] = data


class ProgrammingTests(unittest.TestCase):
    def test_preserves_other_bytes_in_affected_pages(self):
        probe = FakeProbe()
        probe.flash[100] = 0x42
        probe.flash[PAGE_SIZE + 5] = 0x43
        program_pages(probe, {101: 0x12, PAGE_SIZE + 6: 0x34})
        self.assertEqual(probe.erased, [0, 1])
        self.assertEqual(probe.flash[100:102], b"\x42\x12")
        self.assertEqual(probe.flash[PAGE_SIZE + 5:PAGE_SIZE + 7], b"\x43\x34")

    def test_skips_matching_page(self):
        probe = FakeProbe()
        program_pages(probe, {123: 0xff})
        self.assertEqual(probe.erased, [])


if __name__ == "__main__":
    unittest.main()
