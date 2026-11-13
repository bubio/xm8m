#!/usr/bin/env python3
"""Verify the architecture of the actual CTest executable before running it."""
import pathlib
import struct
import sys


def architecture(path):
    data = path.read_bytes()
    if data[:2] == b"MZ":
        offset = struct.unpack_from("<I", data, 0x3C)[0]
        if data[offset:offset + 4] != b"PE\0\0":
            raise ValueError("invalid PE signature")
        machine = struct.unpack_from("<H", data, offset + 4)[0]
        return {0x14C: "x86", 0x8664: "x64", 0xAA64: "arm64"}[machine]
    if data[:4] == b"\xcf\xfa\xed\xfe":
        cpu = struct.unpack_from("<I", data, 4)[0]
        return {0x1000007: "x64", 0x100000C: "arm64"}[cpu]
    raise ValueError("expected a single-architecture PE or Mach-O executable")


if __name__ == "__main__":
    build, expected = pathlib.Path(sys.argv[1]), sys.argv[2]
    candidates = [build / "Release" / "app_state_test.exe", build / "app_state_test"]
    executable = next((path for path in candidates if path.is_file()), None)
    if executable is None:
        sys.exit("app_state_test executable not found")
    actual = architecture(executable)
    print(f"{executable}: {actual} (expected {expected})")
    if actual != expected:
        sys.exit("CTest executable architecture does not match the matrix")
