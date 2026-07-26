SECTION .rodata
GLOBAL _initrd_start
GLOBAL _initrd_end
_initrd_start:
INCBIN "initrd.bin"
_initrd_end:
