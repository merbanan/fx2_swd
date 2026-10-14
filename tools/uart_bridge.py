#!/usr/bin/env python3
"""Use the FX2LP GPIO UART as a byte-oriented bridge (Linux hidraw)."""

import argparse
import os
import select
import sys
import termios
import tty
from pathlib import Path

if __package__:
    from .load_fx2 import LoadError
    from .hid_transport import HidTransport, find_probe
else:
    from load_fx2 import LoadError
    from hid_transport import HidTransport, find_probe


MODES = {"8n1": 0, "updi": 1, "8e1": 2}
ERRORS = {1: "start timeout", 2: "false start", 3: "parity", 4: "stop bit", 5: "UART not configured"}


class UartProbe:
    def __init__(self, path):
        self.hid = HidTransport(path)

    def close(self):
        self.hid.close()

    def configure(self, baud, mode="8n1"):
        if mode not in MODES:
            raise ValueError("UART mode must be 8n1, 8e1, or updi")
        if not 1200 <= baud <= 38400:
            raise ValueError("baud must be 1200..38400")
        if self.hid.exchange(0xa0, bytes([MODES[mode]]) + baud.to_bytes(4, "little"))[1]:
            raise LoadError("UART configuration failed")

    def exchange(self, tx=b"", rx_count=0, timeout_ms=100):
        tx = bytes(tx)
        if len(tx) > 60 or not 0 <= rx_count <= 61:
            raise ValueError("one exchange supports <=60 TX and <=61 RX bytes")
        units = max(1, min(255, (timeout_ms + 15) // 16))
        r = self.hid.exchange(0xa1, bytes([len(tx), rx_count, units]) + tx)
        if r[1]:
            raise LoadError(f"UART {ERRORS.get(r[1], 'error')} after {r[2]} received bytes")
        if r[2] != rx_count:
            raise LoadError(f"UART short read ({r[2]}/{rx_count})")
        return bytes(r[3:3 + rx_count])

    def write(self, data):
        for pos in range(0, len(data), 60):
            self.exchange(data[pos:pos + 60])

    def read(self, count, timeout_ms=100):
        data = bytearray()
        while len(data) < count:
            data.extend(self.exchange(rx_count=min(61, count - len(data)), timeout_ms=timeout_ms))
        return bytes(data)

    def read_available(self):
        r = self.hid.exchange(0xa1, bytes([0, 1, 1]))
        if r[1] == 1:
            return b""
        if r[1] or r[2] != 1:
            raise LoadError(f"UART receive error {r[1]}")
        return bytes(r[3:4])

    def send_break(self, duration_ms=2):
        if not 1 <= duration_ms <= 255:
            raise ValueError("break duration must be 1..255 ms")
        if self.hid.exchange(0xa2, bytes([duration_ms]))[1]:
            raise LoadError("UART break failed")

    def control(self, boot=None, reset=None):
        mask = (boot is not None) | ((reset is not None) << 1)
        value = bool(boot) | (bool(reset) << 1)
        return self.hid.exchange(0xa3, bytes([value, mask]))[1]


def terminal(probe):
    input_fd = sys.stdin.fileno()
    output_fd = sys.stdout.fileno()
    old_settings = termios.tcgetattr(input_fd) if os.isatty(input_fd) else None
    if old_settings is not None:
        tty.setcbreak(input_fd)
    try:
        while True:
            if select.select([input_fd], [], [], 0)[0]:
                data = os.read(input_fd, 60)
                if not data:
                    return
                probe.write(data)
            data = probe.read_available()
            if data:
                os.write(output_fd, data)
    finally:
        if old_settings is not None:
            termios.tcsetattr(input_fd, termios.TCSADRAIN, old_settings)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", type=Path, help="hidraw path; autodetected if omitted")
    parser.add_argument("--baud", type=int, default=9600)
    parser.add_argument("--mode", choices=MODES, default="8n1")
    parser.add_argument("--tx-hex", default="", help="hexadecimal bytes to transmit")
    parser.add_argument("--tx-file", type=Path, help="binary file to transmit after --tx-hex")
    parser.add_argument("--rx", type=int, default=0, help="number of bytes to receive")
    parser.add_argument("--rx-file", type=Path, help="save received bytes")
    parser.add_argument("--timeout-ms", type=int, default=100)
    parser.add_argument("--terminal", action="store_true", help="interactive byte terminal; Ctrl-C exits")
    parser.add_argument("--break-ms", type=int)
    parser.add_argument("--boot", choices=("high", "low"))
    parser.add_argument("--reset", choices=("release", "assert"))
    args = parser.parse_args()
    try:
        tx = bytes.fromhex(args.tx_hex)
        if args.tx_file:
            tx += args.tx_file.read_bytes()
        if args.rx < 0 or args.timeout_ms < 1:
            raise ValueError("receive count and timeout must be nonnegative")
        probe = UartProbe(args.device or find_probe())
        try:
            probe.configure(args.baud, args.mode)
            if args.boot or args.reset:
                probe.control(None if args.boot is None else args.boot == "high",
                              None if args.reset is None else args.reset == "release")
            if args.break_ms:
                probe.send_break(args.break_ms)
            probe.write(tx)
            if args.terminal:
                terminal(probe)
            else:
                data = probe.read(args.rx, args.timeout_ms)
                if args.rx_file:
                    args.rx_file.write_bytes(data)
                elif data:
                    print(data.hex(" "))
        finally:
            probe.close()
    except KeyboardInterrupt:
        return 0
    except (LoadError, OSError, ValueError) as exc:
        parser.exit(1, f"error: {exc}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
