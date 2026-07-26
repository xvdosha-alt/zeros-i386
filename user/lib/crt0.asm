BITS 32
SECTION .text
GLOBAL _start
EXTERN main
EXTERN libmp_exit

_start:
    call main
    push eax
    call libmp_exit
.hang:
    jmp .hang
