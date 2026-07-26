BITS 16
ORG 0x7C00

KERNEL_SEG  equ 0x1000
KERNEL_OFF  equ 0x0000
KERNEL_LBA  equ 1
KERNEL_SECS equ 32
SPT         equ 18
HEADS       equ 2

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl

    mov ax, 0x0003
    int 0x10

    mov ax, KERNEL_SEG
    mov es, ax
    xor bx, bx

    mov si, KERNEL_LBA
    mov di, KERNEL_SECS

.load:
    pusha
    mov ax, si
    xor dx, dx
    mov cx, SPT * HEADS
    div cx
    mov ch, al
    mov ax, dx
    xor dx, dx
    mov cx, SPT
    div cx
    mov dh, al
    mov cl, dl
    inc cl
    mov dl, [boot_drive]
    mov ah, 0x02
    mov al, 1
    int 0x13
    jc .disk_error
    popa
    add bx, 512
    jnc .next
    mov ax, es
    add ax, 0x1000
    mov es, ax
.next:
    inc si
    dec di
    jnz .load

    in al, 0x92
    or al, 2
    out 0x92, al

    cli
    lgdt [gdt_desc]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:pm_entry

.disk_error:
    mov ah, 0x0E
    mov al, 'E'
    int 0x10
    jmp $

BITS 32
pm_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    mov eax, 0x10000
    jmp eax

ALIGN 4
gdt:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF

gdt_desc:
    dw gdt_desc - gdt - 1
    dd gdt

boot_drive:
    db 0

times 510 - ($ - $$) db 0
dw 0xAA55
