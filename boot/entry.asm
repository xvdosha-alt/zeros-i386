BITS 32

MBALIGN  equ 1 << 0
MEMINFO  equ 1 << 1
FLAGS    equ MBALIGN | MEMINFO
MAGIC    equ 0x1BADB002
CHECKSUM equ -(MAGIC + FLAGS)
STACK_SIZE equ 32768

SECTION .multiboot
align 4
    dd MAGIC
    dd FLAGS
    dd CHECKSUM

SECTION .bss
align 16
stack_bottom:
    resb STACK_SIZE
stack_top:

align 4
tss:
    resd 26
mbi_ptr:
    resd 1

SECTION .data
align 8
gdt_start:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
    dq 0x00CFFA000000FFFF
    dq 0x00CFF2000000FFFF
gdt_tss:
    dw 0
    dw 0
    db 0
    db 0x89
    db 0
    db 0
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

SECTION .text
GLOBAL _start
GLOBAL tss_set_stack
GLOBAL gdt_flush_tss
GLOBAL jump_to_user
GLOBAL resume_user_frame
GLOBAL irq0_stub
GLOBAL irq1_stub
GLOBAL irq12_stub
GLOBAL isr_stub
GLOBAL syscall_stub
GLOBAL stack_top
EXTERN kernel_main
EXTERN timer_irq
EXTERN kbd_irq_handler
EXTERN mouse_irq_handler
EXTERN syscall_handler
EXTERN sched_on_timer

_start:
    cli
    mov [mbi_ptr], ebx

    mov eax, tss
    mov word [gdt_tss], 104
    mov word [gdt_tss + 2], ax
    shr eax, 16
    mov byte [gdt_tss + 4], al
    mov byte [gdt_tss + 7], ah
    mov byte [gdt_tss + 5], 0x89
    mov byte [gdt_tss + 6], 0x00

    lgdt [gdt_descriptor]
    jmp 0x08:.reload_cs

.reload_cs:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, stack_top
    mov dword [tss + 4], stack_top
    mov dword [tss + 8], 0x10
    mov word [tss + 102], 104
    mov ax, 0x28
    ltr ax
    push dword [mbi_ptr]
    call kernel_main
    add esp, 4
.hang:
    cli
    hlt
    jmp .hang

tss_set_stack:
    mov eax, [esp + 4]
    mov [tss + 4], eax
    mov dword [tss + 8], 0x10
    ret

gdt_flush_tss:
    mov ax, 0x28
    ltr ax
    ret

jump_to_user:
    cli
    mov eax, [esp + 4]
    mov ecx, [esp + 8]
    mov edx, [esp + 12]
    push dword 0x23
    push ecx
    pushf
    pop ebx
    or ebx, 0x200
    push ebx
    push dword 0x1B
    push eax
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov eax, edx
    iret

resume_user_frame:
    cli
    mov esp, [esp + 4]
    popa
    pop gs
    pop fs
    pop es
    pop ds
    iret

irq0_stub:
    pusha
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    call timer_irq
    call sched_on_timer
    mov al, 0x20
    out 0x20, al
    pop gs
    pop fs
    pop es
    pop ds
    popa
    iret

irq1_stub:
    pusha
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    call kbd_irq_handler
    mov al, 0x20
    out 0x20, al
    pop gs
    pop fs
    pop es
    pop ds
    popa
    iret

irq12_stub:
    pusha
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    call mouse_irq_handler
    mov al, 0x20
    out 0xA0, al
    out 0x20, al
    pop gs
    pop fs
    pop es
    pop ds
    popa
    iret

isr_stub:
    cli
    hlt
    jmp isr_stub

syscall_stub:
    push ds
    push es
    push fs
    push gs
    pusha
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    cli
    push esp
    call syscall_handler
    add esp, 4
    mov [esp + 28], eax
    popa
    pop gs
    pop fs
    pop es
    pop ds
    iret
