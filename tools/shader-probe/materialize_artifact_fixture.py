#!/usr/bin/env python3
"""Verify the reviewed X3SP fixture hex, or materialize its binary explicitly."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import struct
import sys


ROOT = Path(__file__).resolve().parents[2]
HEX_PATH = ROOT / "tests" / "shaders" / "artifacts" / "minimal-v1.hex"
BIN_PATH = ROOT / "tests" / "shaders" / "artifacts" / "minimal-v1.x3sp"
SIZE = 668
EXPECTED_VS = bytes.fromhex("1951763138c86ef2059da0f8cfdd380310c4f0ae4d89a88e6bb163a013a749bb")
EXPECTED_PS = bytes.fromhex("87fba55c4bf7c7ef9b8e681b2a2a6c3946967469f31997ed644db73c0b3e34b9")
EXPECTED_FILE = bytes.fromhex("955313d63bf7a11b633805695ad5fcd19c5ca21b4f7d139fa6b4548be25d0cdd")


def verify(data: bytes) -> None:
    if len(data) != SIZE:
        raise ValueError(f"fixture is {len(data)} bytes; expected {SIZE}")
    if data[:4] != b"X3SP" or struct.unpack_from("<I", data, 12)[0] != SIZE:
        raise ValueError("fixture envelope does not match the reviewed v1 recipe")
    # Spell out compiler-order words independently of the LE file slice.
    vs_preimage = bytes.fromhex("1234567801020304a0b0c0d0")
    ps_preimage = bytes.fromhex("87654321040302010d0c0b0a")
    if hashlib.sha256(vs_preimage).digest() != EXPECTED_VS:
        raise ValueError("independent VS digest recipe changed")
    if hashlib.sha256(ps_preimage).digest() != EXPECTED_PS:
        raise ValueError("independent PS digest recipe changed")
    if data[360:392] != EXPECTED_VS or data[520:552] != EXPECTED_PS:
        raise ValueError("stage digests in fixture do not match the independent recipe")
    digest_input = bytearray(data)
    digest_input[32:64] = bytes(32)
    if hashlib.sha256(digest_input).digest() != EXPECTED_FILE:
        raise ValueError("artifact digest does not match the independent recipe")
    if data[656:667] != b"fixtureVSPS" or data[667] != 0:
        raise ValueError("fixture string or final zero padding is incorrect")
    if data[560:572] != bytes.fromhex("7856341204030201d0c0b0a0"):
        raise ValueError("VS words are not the reviewed LE representation")
    if data[572:584] != bytes.fromhex("21436587010203040a0b0c0d"):
        raise ValueError("PS words are not the reviewed LE representation")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--verify", action="store_true", help="verify hex and existing binary (default)")
    parser.add_argument("--write", action="store_true", help="materialize binary from reviewed literal hex")
    args = parser.parse_args()
    if args.verify and args.write:
        parser.error("choose either --verify or --write")
    data = bytes.fromhex(HEX_PATH.read_text(encoding="ascii"))
    verify(data)
    if args.write:
        BIN_PATH.write_bytes(data)
        print(f"wrote {BIN_PATH.relative_to(ROOT)} ({len(data)} bytes)")
    else:
        if not BIN_PATH.exists() or BIN_PATH.read_bytes() != data:
            raise ValueError("binary fixture is missing or differs from reviewed hex; use --write explicitly")
        print(f"verified {HEX_PATH.relative_to(ROOT)} and {BIN_PATH.relative_to(ROOT)} ({len(data)} bytes)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f"fixture verification failed: {error}", file=sys.stderr)
        raise SystemExit(1)
