#!/usr/bin/env python3
import os
import struct
import sys

def add_dir(entries, path):
    entries.append((path, None))

def add_file(entries, path, data):
    entries.append((path, data))

def walk(root, prefix, entries):
    for name in sorted(os.listdir(root)):
        if name.startswith('.'):
            continue
        full = os.path.join(root, name)
        rel = prefix + '/' + name if prefix else '/' + name
        if os.path.isdir(full):
            add_dir(entries, rel)
            walk(full, rel, entries)
        else:
            with open(full, 'rb') as f:
                add_file(entries, rel, f.read())

def pack(entries, out_path):
    blob = bytearray()
    for path, data in entries:
        pb = path.encode('utf-8')
        if data is None:
            blob += struct.pack('<II', len(pb), 0xFFFFFFFF)
            blob += pb
        else:
            blob += struct.pack('<II', len(pb), len(data))
            blob += pb
            blob += data
    with open(out_path, 'wb') as f:
        f.write(blob)
    print('initrd', out_path, len(blob), 'bytes', len(entries), 'entries')

def main():
    src = sys.argv[1]
    out = sys.argv[2]
    entries = []
    walk(src, '', entries)
    pack(entries, out)

if __name__ == '__main__':
    main()
