#!/bin/sh
set -eu

DISK="${1:-/dev/disk4}"
ISO="$(cd "$(dirname "$0")" && pwd)/out/grub.iso"

if [ ! -f "$ISO" ]; then
    echo "missing $ISO — run: make grub.iso-docker" >&2
    exit 1
fi

if [ "$(id -u)" -ne 0 ]; then
    echo "re-run with sudo: sudo $0 $DISK" >&2
    exit 1
fi

case "$DISK" in
    /dev/disk[0-9]*|/dev/rdisk[0-9]*) ;;
    *)
        echo "expected /dev/diskN or /dev/rdiskN, got: $DISK" >&2
        exit 1
        ;;
esac

RDISK="$DISK"
case "$DISK" in
    /dev/disk*) RDISK="/dev/r${DISK#/dev/}" ;;
esac

if diskutil info "$DISK" | grep -q 'Protocol:.*Internal'; then
    echo "refusing to write to internal disk: $DISK" >&2
    exit 1
fi

echo "writing $ISO -> $RDISK"
diskutil unmountDisk "$DISK"
dd if="$ISO" of="$RDISK" bs=1m conv=sync
sync
diskutil eject "$DISK"
echo "done"
