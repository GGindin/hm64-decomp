#!/usr/bin/env python3
"""Extract boot-safe ucode windows that splat cannot model directly."""

from __future__ import annotations

import argparse
from pathlib import Path


GSP_F3DEX2_TEXT_OFFSET = 0xEC950
GSP_F3DEX2_TEXT_SIZE = 0x2200
GSP_F3DEX2_DATA_OFFSET = 0xF9C40
GSP_F3DEX2_DATA_SIZE = 0x800

GSP_F3DEX2_TEXT_OUT = Path("lib/ucode/gspF3DEX2.fifo.textbin.bin")
GSP_F3DEX2_DATA_OUT = Path("lib/ucode/gspF3DEX2.fifo.databin.bin")


def extract_window(rom: bytes, offset: int, size: int, output_path: Path) -> None:
    end = offset + size
    if len(rom) < end:
        raise ValueError(
            f"{output_path}: baserom is too small for window "
            f"0x{offset:X}..0x{end:X}"
        )

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(rom[offset:end])


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baserom", type=Path, help="Path to baserom z64")
    args = parser.parse_args()

    if not args.baserom.exists():
        raise FileNotFoundError(f"Missing baserom: {args.baserom}")

    rom = args.baserom.read_bytes()
    extract_window(
        rom,
        GSP_F3DEX2_TEXT_OFFSET,
        GSP_F3DEX2_TEXT_SIZE,
        GSP_F3DEX2_TEXT_OUT,
    )
    extract_window(
        rom,
        GSP_F3DEX2_DATA_OFFSET,
        GSP_F3DEX2_DATA_SIZE,
        GSP_F3DEX2_DATA_OUT,
    )

    print(
        "Extracted F3DEX2 ucode windows: "
        f"text=0x{GSP_F3DEX2_TEXT_SIZE:X}, data=0x{GSP_F3DEX2_DATA_SIZE:X}"
    )


if __name__ == "__main__":
    main()
