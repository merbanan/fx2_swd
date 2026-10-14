#!/usr/bin/env python3
"""Clock a JTAG TAP and scan its IDCODE data register with CMSIS-DAP."""

import argparse
from pathlib import Path

if __package__:
    from .load_fx2 import LoadError
    from .hid_transport import HidTransport, find_probe
else:
    from load_fx2 import LoadError
    from hid_transport import HidTransport, find_probe


class JtagProbe:
    def __init__(self, path):
        self.hid = HidTransport(path)

    def close(self):
        self.hid.exchange(0x03)
        self.hid.close()

    def connect(self):
        if self.hid.exchange(0x02, b"\x02")[1] != 2:
            raise LoadError("JTAG connect failed")

    def sequence(self, bits, tms, tdi=0, capture=False):
        if not 1 <= bits <= 48:
            raise ValueError("JTAG sequence must contain 1..48 bits")
        n = (bits + 7) // 8
        info = (bits & 63) | (0x40 if tms else 0) | (0x80 if capture else 0)
        response = self.hid.exchange(0x14, bytes([1, info]) +
                                     (tdi & ((1 << bits) - 1)).to_bytes(n, "little"))
        if response[1]:
            raise LoadError("JTAG sequence failed")
        return int.from_bytes(response[2:2 + n], "little") if capture else 0

    def reset(self):
        self.sequence(5, True, 0x1f)
        self.sequence(1, False)

    def scan_dr(self, bits, data=0):
        if bits < 1:
            raise ValueError("scan length must be positive")
        self.sequence(1, True)   # Select-DR
        self.sequence(1, False)  # Capture-DR
        self.sequence(1, False)  # Shift-DR
        result = 0
        shifted = 0
        while bits - shifted > 1:
            count = min(48, bits - shifted - 1)
            part = self.sequence(count, False, data >> shifted, True)
            result |= part << shifted
            shifted += count
        result |= self.sequence(1, True, data >> shifted, True) << shifted
        self.sequence(1, True)   # Update-DR
        self.sequence(1, False)  # Idle
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", type=Path)
    parser.add_argument("--bits", type=int, default=128,
                        help="number of DR bits to sample after TAP reset")
    args = parser.parse_args()
    try:
        probe = JtagProbe(args.device or find_probe())
        try:
            probe.connect()
            probe.reset()
            result = probe.scan_dr(args.bits)
            print(f"DR: 0x{result:0{(args.bits + 3) // 4}x} (LSB shifted first)")
            for offset in range(0, args.bits - 31, 32):
                word = (result >> offset) & 0xffffffff
                print(f"bits {offset:3d}..{offset + 31:3d}: 0x{word:08x}")
        finally:
            probe.close()
    except (LoadError, OSError, ValueError) as exc:
        parser.exit(1, f"error: {exc}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
