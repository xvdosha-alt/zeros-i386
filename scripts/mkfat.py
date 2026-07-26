#!/usr/bin/env python3
"""Create a minimal FAT16 disk image for zerOS /disk."""
import os
import struct
import sys


def write_fat16_manual(path, size_mb):
    bps = 512
    spc = 4
    reserved = 1
    fats = 2
    root_ents = 512
    tot = size_mb * 1024 * 1024 // bps
    root_secs = (root_ents * 32 + bps - 1) // bps
    # fat size: enough for all clusters
    data_secs = tot - reserved - root_secs
    # iterative fatsz
    fatsz = 1
    while True:
        data = tot - reserved - fats * fatsz - root_secs
        clusters = data // spc
        need = (clusters * 2 + bps - 1) // bps
        if need <= fatsz:
            break
        fatsz = need
    with open(path, "r+b") as f:
        bpb = bytearray(bps)
        bpb[0:3] = b"\xEB\x3C\x90"
        bpb[3:11] = b"MSDOS5.0"
        struct.pack_into("<H", bpb, 11, bps)
        bpb[13] = spc
        struct.pack_into("<H", bpb, 14, reserved)
        bpb[16] = fats
        struct.pack_into("<H", bpb, 17, root_ents)
        if tot < 65536:
            struct.pack_into("<H", bpb, 19, tot)
        else:
            struct.pack_into("<H", bpb, 19, 0)
            struct.pack_into("<I", bpb, 32, tot)
        bpb[21] = 0xF8
        struct.pack_into("<H", bpb, 22, fatsz)
        struct.pack_into("<H", bpb, 24, 32)  # spt
        struct.pack_into("<H", bpb, 26, 2)   # heads
        bpb[36] = 0x80
        bpb[38] = 0x29
        bpb[39:43] = b"\x12\x34\x56\x78"
        bpb[43:54] = b"ZEROS      "
        bpb[54:62] = b"FAT16   "
        bpb[510] = 0x55
        bpb[511] = 0xAA
        f.seek(0)
        f.write(bpb)
        fat = bytearray(fatsz * bps)
        fat[0] = 0xF8
        fat[1] = 0xFF
        fat[2] = 0xFF
        fat[3] = 0xFF
        # cluster 2 = EOC for HELLO.TXT
        fat[4] = 0xFF
        fat[5] = 0xFF
        for i in range(fats):
            f.seek((reserved + i * fatsz) * bps)
            f.write(fat)
        root = bytearray(root_secs * bps)
        root[0:11] = b"HELLO   TXT"
        root[11] = 0x20
        root[26:28] = struct.pack("<H", 2)
        content = b"hello from FAT volume on zerOS\n"
        root[28:32] = struct.pack("<I", len(content))
        f.seek((reserved + fats * fatsz) * bps)
        f.write(root)
        data_start = reserved + fats * fatsz + root_secs
        f.seek(data_start * bps)
        cluster = bytearray(spc * bps)
        cluster[0 : len(content)] = content
        f.write(cluster)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "out/fat.img"
    size_mb = 16
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    with open(out, "wb") as f:
        f.write(b"\0" * (size_mb * 1024 * 1024))
    write_fat16_manual(out, size_mb)
    # Optional overlay via mtools
    seed = os.path.join(os.path.dirname(__file__), "..", "disk", "seed")
    if os.path.isdir(seed):
        for name in sorted(os.listdir(seed)):
            src = os.path.join(seed, name)
            if os.path.isfile(src):
                os.system(f"mcopy -o -i {out} {src} ::/{name} >/dev/null 2>&1")
    print(f"fat image {out} ({size_mb} MiB)")


if __name__ == "__main__":
    main()
