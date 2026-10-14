#!/usr/bin/env python3
"""Load an SDCC Intel HEX image into a blank FX2LP over its A0 boot request."""

import argparse
import ctypes as c
import ctypes.util
from pathlib import Path


class LoadError(Exception):
    pass


def parse_hex(path, max_address=0x3e00):
    image = {}
    upper = 0
    ended = False
    for line_no, text in enumerate(Path(path).read_text(encoding="ascii").splitlines(), 1):
        if not text:
            continue
        if ended:
            raise LoadError(f"line {line_no}: data after EOF")
        if not text.startswith(":"):
            raise LoadError(f"line {line_no}: expected ':'")
        try:
            record = bytes.fromhex(text[1:])
        except ValueError as exc:
            raise LoadError(f"line {line_no}: invalid hex") from exc
        if len(record) < 5 or len(record) != record[0] + 5 or sum(record) & 0xff:
            raise LoadError(f"line {line_no}: bad length or checksum")
        count = record[0]
        address = (record[1] << 8) | record[2]
        kind = record[3]
        payload = record[4:-1]
        if kind == 0:
            for offset, byte in enumerate(payload):
                location = upper + address + offset
                if location >= max_address:
                    raise LoadError(f"line {line_no}: address 0x{location:04x} outside firmware area")
                if location in image and image[location] != byte:
                    raise LoadError(f"line {line_no}: conflicting data at 0x{location:04x}")
                image[location] = byte
        elif kind == 1:
            if count or address:
                raise LoadError(f"line {line_no}: invalid EOF")
            ended = True
        elif kind == 2 and count == 2 and address == 0:
            upper = int.from_bytes(payload, "big") << 4
        elif kind == 4 and count == 2 and address == 0:
            upper = int.from_bytes(payload, "big") << 16
        elif kind in (3, 5):
            pass  # start address is not used by the FX2 ROM loader
        else:
            raise LoadError(f"line {line_no}: unsupported record type {kind}")
    if not ended or not image:
        raise LoadError("missing EOF or empty image")
    return image


def chunks(image, size=64):
    addresses = sorted(image)
    start = addresses[0]
    previous = start - 1
    data = bytearray()
    for address in addresses:
        if data and (address != previous + 1 or len(data) == size):
            yield start, bytes(data)
            start, data = address, bytearray()
        data.append(image[address])
        previous = address
    if data:
        yield start, bytes(data)


class DeviceDescriptor(c.Structure):
    _fields_ = [
        ("bLength", c.c_uint8), ("bDescriptorType", c.c_uint8),
        ("bcdUSB", c.c_uint16), ("bDeviceClass", c.c_uint8),
        ("bDeviceSubClass", c.c_uint8), ("bDeviceProtocol", c.c_uint8),
        ("bMaxPacketSize0", c.c_uint8), ("idVendor", c.c_uint16),
        ("idProduct", c.c_uint16), ("bcdDevice", c.c_uint16),
        ("iManufacturer", c.c_uint8), ("iProduct", c.c_uint8),
        ("iSerialNumber", c.c_uint8), ("bNumConfigurations", c.c_uint8),
    ]


class LibUSB:
    def __init__(self):
        name = ctypes.util.find_library("usb-1.0")
        if not name:
            raise LoadError("libusb-1.0 is required")
        self.api = c.CDLL(name)
        api = self.api
        api.libusb_init.argtypes = [c.POINTER(c.c_void_p)]
        api.libusb_init.restype = c.c_int
        api.libusb_exit.argtypes = [c.c_void_p]
        api.libusb_get_device_list.argtypes = [c.c_void_p, c.POINTER(c.POINTER(c.c_void_p))]
        api.libusb_get_device_list.restype = c.c_ssize_t
        api.libusb_free_device_list.argtypes = [c.POINTER(c.c_void_p), c.c_int]
        api.libusb_get_device_descriptor.argtypes = [c.c_void_p, c.POINTER(DeviceDescriptor)]
        api.libusb_get_device_descriptor.restype = c.c_int
        api.libusb_get_bus_number.argtypes = [c.c_void_p]
        api.libusb_get_bus_number.restype = c.c_uint8
        api.libusb_get_device_address.argtypes = [c.c_void_p]
        api.libusb_get_device_address.restype = c.c_uint8
        api.libusb_open.argtypes = [c.c_void_p, c.POINTER(c.c_void_p)]
        api.libusb_open.restype = c.c_int
        api.libusb_close.argtypes = [c.c_void_p]
        api.libusb_control_transfer.argtypes = [c.c_void_p, c.c_uint8, c.c_uint8,
                                                c.c_uint16, c.c_uint16,
                                                c.POINTER(c.c_uint8), c.c_uint16, c.c_uint]
        api.libusb_control_transfer.restype = c.c_int
        self.context = c.c_void_p()
        result = api.libusb_init(c.byref(self.context))
        if result < 0:
            raise LoadError(f"libusb_init failed ({result})")
        self.handle = c.c_void_p()

    def open(self, vid, pid, bus=None, address=None, force=False):
        devices = c.POINTER(c.c_void_p)()
        total = self.api.libusb_get_device_list(self.context, c.byref(devices))
        if total < 0:
            raise LoadError(f"USB enumeration failed ({total})")
        try:
            matches = []
            for index in range(total):
                dev = devices[index]
                desc = DeviceDescriptor()
                if self.api.libusb_get_device_descriptor(dev, c.byref(desc)) < 0:
                    continue
                if desc.idVendor != vid or desc.idProduct != pid:
                    continue
                dev_bus = self.api.libusb_get_bus_number(dev)
                dev_address = self.api.libusb_get_device_address(dev)
                if bus is not None and dev_bus != bus:
                    continue
                if address is not None and dev_address != address:
                    continue
                matches.append((dev, desc, dev_bus, dev_address))
            if not matches:
                raise LoadError(f"no FX2LP found at {vid:04x}:{pid:04x}")
            if len(matches) != 1:
                locations = ", ".join(f"{b}:{a}" for _, _, b, a in matches)
                raise LoadError(f"multiple devices ({locations}); select --bus and --address")
            dev, desc, dev_bus, dev_address = matches[0]
            if desc.iProduct and not force:
                raise LoadError("device has a product string and may already run firmware; use --force if intentional")
            result = self.api.libusb_open(dev, c.byref(self.handle))
            if result < 0:
                raise LoadError(f"cannot open USB device {dev_bus}:{dev_address} ({result})")
            return dev_bus, dev_address
        finally:
            self.api.libusb_free_device_list(devices, 1)

    def transfer(self, direction, address, payload):
        length = len(payload)
        buffer = (c.c_uint8 * length).from_buffer_copy(payload) if not direction else (c.c_uint8 * length)()
        result = self.api.libusb_control_transfer(self.handle, 0xc0 if direction else 0x40,
                                                  0xa0, address, 0, buffer, length, 2000)
        if result != length:
            raise LoadError(f"USB A0 transfer at 0x{address:04x} failed ({result}/{length})")
        return bytes(buffer)

    def close(self):
        if self.handle:
            self.api.libusb_close(self.handle)
            self.handle = c.c_void_p()
        if self.context:
            self.api.libusb_exit(self.context)
            self.context = c.c_void_p()


def load(image, usb, verify=True):
    usb.transfer(False, 0xe600, b"\x01")  # hold 8051 in reset
    for address, data in chunks(image):
        usb.transfer(False, address, data)
        if verify and usb.transfer(True, address, data) != data:
            raise LoadError(f"verification failed at 0x{address:04x}")
    usb.transfer(False, 0xe600, b"\x00")  # run the new firmware


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="SDCC Intel HEX file")
    parser.add_argument("--check", action="store_true", help="validate image without USB access")
    parser.add_argument("--vid", type=lambda x: int(x, 0), default=0x04b4)
    parser.add_argument("--pid", type=lambda x: int(x, 0), default=0x8613)
    parser.add_argument("--bus", type=int)
    parser.add_argument("--address", type=int)
    parser.add_argument("--force", action="store_true", help="allow a device with a product string")
    parser.add_argument("--no-verify", action="store_true", help="skip USB readback")
    args = parser.parse_args()
    try:
        image = parse_hex(args.image)
        print(f"validated {len(image)} bytes at 0x{min(image):04x}..0x{max(image):04x}")
        if args.check:
            return 0
        usb = LibUSB()
        try:
            bus, address = usb.open(args.vid, args.pid, args.bus, args.address, args.force)
            print(f"loading FX2LP at USB {bus}:{address}")
            load(image, usb, not args.no_verify)
            print("firmware started; device will re-enumerate as FX2LP CMSIS-DAP")
        finally:
            usb.close()
    except (LoadError, OSError) as exc:
        parser.exit(1, f"error: {exc}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
