.set MAGIC, 0x1badb002
.set FLAGS, (1 << 0 | 1 << 1 | 1 << 2)
.set CHECKNUM, -(MAGIC + FLAGS)

.section .multiboot
    .long MAGIC
    .long FLAGS
    .long CHECKNUM
    .long 0, 0, 0, 0, 0
    .long 0
    .long 800
    .long 600
    .long 32

.section .text
.extern kernelMain
.extern callConstructors
.global loader

loader:
    mov $kernel_stack, %esp
    mov %eax, %esi
    mov %ebx, %edi
    call callConstructors
    push %esi
    push %edi
    call kernelMain

_stop:
    cli
    hlt
    jmp _stop


.section .bss
.space 2*1024*1024
kernel_stack:
