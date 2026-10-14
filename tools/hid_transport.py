"""Linux hidraw transport for the 64-byte FX2LP vendor commands."""

import os
import select
from pathlib import Path

try:
    from .load_fx2 import LoadError
except ImportError:
    from load_fx2 import LoadError


def find_probe():
    paths = []
    for entry in Path("/sys/class/hidraw").glob("hidraw*"):
        try:
            info = (entry / "device/uevent").read_text()
        except OSError:
            continue
        if "FX2LP CMSIS-DAP" in info:
            paths.append(Path("/dev") / entry.name)
    if len(paths) != 1:
        raise LoadError(f"expected one FX2LP CMSIS-DAP hidraw device, found {len(paths)}; use --device")
    return paths[0]


class HidTransport:
    def __init__(self, path):
        self.fd = os.open(path, os.O_RDWR)

    def close(self):
        os.close(self.fd)

    def exchange(self, command, args=b""):
        if len(args) > 63:
            raise LoadError("command exceeds HID report")
        report = bytes([command]) + args
        sent = os.write(self.fd, b"\x00" + report.ljust(64, b"\x00"))
        if sent != 65:
            raise LoadError(f"short HID write ({sent}/65)")
        if not select.select([self.fd], [], [], 15)[0]:
            raise LoadError(f"command 0x{command:02x} timed out")
        response = os.read(self.fd, 64)
        if len(response) != 64 or response[0] != command:
            raise LoadError(f"invalid response to 0x{command:02x}")
        return response
