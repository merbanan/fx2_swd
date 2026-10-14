import unittest

from tools.load_fx2 import LoadError
from tools.pic16_icsp import program as pic_program
from tools.stm32_uart import FLASH_BASE, Stm32Bootloader, program as stm32_program
from tools.updi_program import ACK, FLASH_BASE as UPDI_FLASH_BASE, UpdiProbe, program as updi_program


class FakeUart:
    def __init__(self, replies=b""):
        self.writes = []
        self.replies = bytearray(replies)

    def write(self, data):
        self.writes.append(bytes(data))

    def read(self, count, timeout_ms=100):
        result = bytes(self.replies[:count])
        del self.replies[:count]
        return result

    def exchange(self, tx=b"", rx_count=0):
        self.writes.append(bytes(tx))
        return bytes([ACK] * rx_count)


class Stm32Tests(unittest.TestCase):
    def test_command_write_framing(self):
        uart = FakeUart(bytes([0x79] * 3))
        boot = Stm32Bootloader(uart)
        boot.commands = {0x31}
        boot.write(FLASH_BASE, b"\x01\x02\x03\x04")
        self.assertEqual(uart.writes, [b"\x31\xce", b"\x08\x00\x00\x00\x08",
                                       b"\x03\x01\x02\x03\x04\x07"])

    def test_sparse_image_writes_only_touched_blocks(self):
        class MemoryBoot:
            def __init__(self):
                self.memory = {}
                self.writes = []
                self.erased = False

            def erase(self):
                self.erased = True
                self.memory.clear()

            def write(self, address, data):
                self.writes.append((address, data))
                self.memory.update((address + i, value) for i, value in enumerate(data))

            def read(self, address, count):
                return bytes(self.memory.get(address + i, 0xff) for i in range(count))

        boot = MemoryBoot()
        stm32_program(boot, {0: 0x11, 121: 0x22})
        self.assertTrue(boot.erased)
        self.assertEqual([address for address, _ in boot.writes],
                         [FLASH_BASE, FLASH_BASE + 120])


class UpdiTests(unittest.TestCase):
    def test_key_byte_order_and_word_store(self):
        uart = FakeUart()
        probe = UpdiProbe(uart)
        probe.key(0x4e564d50726f6720)
        probe.sts16(0x8000, 0x1234)
        self.assertEqual(uart.writes[0], b"\x55\xe0 gorPMVN")
        self.assertEqual(uart.writes[1], b"\x55\x45\x00\x80")
        self.assertEqual(uart.writes[2], b"\x34\x12")

    def test_sparse_page_preserves_unmentioned_bytes(self):
        class MemoryUpdi:
            def __init__(self):
                self.flash = bytearray([0xff] * 64)
                self.flash[1] = 0x34

            def enter_programming(self):
                pass

            def read(self, address, count):
                offset = address - UPDI_FLASH_BASE
                return bytes(self.flash[offset:offset + count])

            def write_page(self, base, data):
                self.flash[base:base + len(data)] = data

            def reset(self):
                pass

        probe = MemoryUpdi()
        updi_program(probe, {0: 0x12})
        self.assertEqual(probe.flash[:2], b"\x12\x34")


class PicTests(unittest.TestCase):
    def test_sparse_row_preserves_unmentioned_words(self):
        class MemoryPic:
            def __init__(self):
                self.memory = [0x3fff] * 64
                self.memory[1] = 0x1234
                self.writes = []

            def read_words(self, base, count):
                return self.memory[base:base + count]

            def write_row(self, base, words):
                self.writes.append(base)
                self.memory[base:base + len(words)] = words

        probe = MemoryPic()
        pic_program(probe, {0: 0x42, 1: 0x20})
        self.assertEqual(probe.memory[0], 0x2042)
        self.assertEqual(probe.memory[1], 0x1234)
        self.assertEqual(probe.writes, [0])
        with self.assertRaises(LoadError):
            pic_program(probe, {0: 0x42, 1: 0x21}, verify_only=True)


if __name__ == "__main__":
    unittest.main()
