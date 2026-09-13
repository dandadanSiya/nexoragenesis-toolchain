; BOOTSTRAP_EXTERNAL_ASM
; NOVA_COMPUTE_R0_EXTERNAL_PROBE — UEFI pre-kernel, QEMU only.
; nasm -f bin bootstrap/quarantine/nasm/nova-compute-cpu-r0-r1.asm -o bootstrap/quarantine/nasm/out/nova-compute-cpu-r0-r1.efi

BITS 64
ORG 0

%define IMAGE_BASE              0x0000000140000000
%define EFI_SUCCESS             0
%define EFI_DEVICE_ERROR        0x8000000000000007
%define EFI_ABORTED             0x8000000000000015
%define EFI_COMPROMISED_DATA    0x8000000000000021
%define VECTOR_COUNT            8

SECTION .header start=0x0000 vstart=0x0000 align=1

    db 'M','Z'
    times 0x3C - ($ - $$) db 0
    dd 0x80
    times 0x80 - ($ - $$) db 0

    db 'P','E',0,0
    dw 0x8664
    dw 4
    dd 0,0,0
    dw 0x00F0
    dw 0x0022

    dw 0x020B
    db 0,0
    dd 0x0200
    dd 0x0600
    dd 0
    dd entry
    dd 0x1000
    dq IMAGE_BASE
    dd 0x1000
    dd 0x0200
    dw 0,0,0,0,0,0
    dd 0
    dd 0x5000
    dd 0x0400
    dd 0
    dw 10
    dw 0x0140
    dq 0x100000,0x1000,0x100000,0x1000
    dd 0
    dd 16

    times 5 dq 0
    dd 0x4000, 0x000C
    times 10 dq 0

    db '.text',0,0,0
    dd text_end - text_start
    dd 0x1000
    dd 0x0200
    dd 0x0400
    dd 0,0
    dw 0,0
    dd 0x60000020

    db '.rdata',0,0
    dd rdata_end - rdata_start
    dd 0x2000
    dd 0x0200
    dd 0x0600
    dd 0,0
    dw 0,0
    dd 0x40000040

    db '.data',0,0,0
    dd data_end - data_start
    dd 0x3000
    dd 0x0200
    dd 0x0800
    dd 0,0
    dw 0,0
    dd 0xC0000040

    db '.reloc',0,0
    dd reloc_end - reloc_start
    dd 0x4000
    dd 0x0200
    dd 0x0A00
    dd 0,0
    dw 0,0
    dd 0x42000040

    times 0x400 - ($ - $$) db 0

SECTION .text start=0x0400 vstart=0x1000 align=16
text_start:
entry:
    sub rsp, 0x38
    test rdx, rdx
    jz .no_console
    mov rax, [rdx + 0x40]
    test rax, rax
    jz .no_console
    mov [rsp + 0x20], rax
    mov rax, [rax + 0x08]
    test rax, rax
    jz .no_console
    mov [rsp + 0x28], rax

    lea r8, [rel vector_a]
    lea r9, [rel vector_b]
    lea r10, [rel out_vec]
    xor r11d, r11d
.compute_loop:
    cmp r11d, VECTOR_COUNT
    jae .compute_done
    mov eax, [r8 + r11 * 4]
    add eax, [r9 + r11 * 4]
    mov [r10 + r11 * 4], eax
    inc r11d
    jmp .compute_loop

.compute_done:
%ifdef R0_FAULT_RESULT
    xor dword [rel out_vec + 5 * 4], 1
%endif
%ifdef R0_FAULT_GUARD
    xor dword [rel guard_hi], 1
%endif

    cmp dword [rel guard_lo], 0xC0DEC0DE
    jne .guard_fail
    cmp dword [rel guard_hi], 0xDEC0ADDE
    jne .guard_fail

    lea r8, [rel out_vec]
    lea r9, [rel vector_expected]
    xor r11d, r11d
.verify_loop:
    cmp r11d, VECTOR_COUNT
    jae .result_ok
    mov eax, [r8 + r11 * 4]
    cmp eax, [r9 + r11 * 4]
    jne .result_fail
    inc r11d
    jmp .verify_loop

.result_ok:
    mov qword [rsp + 0x30], EFI_SUCCESS
    lea rdx, [rel msg_ok]
    jmp .report
.result_fail:
    mov rax, EFI_ABORTED
    mov [rsp + 0x30], rax
    lea rdx, [rel msg_e201]
    jmp .report
.guard_fail:
    mov rax, EFI_COMPROMISED_DATA
    mov [rsp + 0x30], rax
    lea rdx, [rel msg_e202]

.report:
    mov rcx, [rsp + 0x20]
    mov rax, [rsp + 0x28]
    call rax
    test rax, rax
    jnz .epilogue
    mov rax, [rsp + 0x30]
    jmp .epilogue

.no_console:
    mov rax, EFI_DEVICE_ERROR
.epilogue:
    add rsp, 0x38
    ret
text_end:
    %if (text_end - text_start) > 0x0200
        %error "NC-R0 .text depasse 0x200"
    %endif
    times 0x0200 - ($ - $$) db 0

SECTION .rdata start=0x0600 vstart=0x2000 align=16
rdata_start:
vector_a:
    dd 0x00000000,0x00000001,0xFFFFFFFF,0x80000000
    dd 0x7FFFFFFF,0x12345678,0xAAAAAAAA,0xDEADBEEF
vector_b:
    dd 0x00000000,0xFFFFFFFF,0x00000001,0x80000000
    dd 0x00000001,0x11111111,0x55555555,0x21524111
vector_expected:
    dd 0x00000000,0x00000000,0x00000000,0x00000000
    dd 0x80000000,0x23456789,0xFFFFFFFF,0x00000000
msg_ok:
    dw 'N','G','_','N','O','V','A','_','C','P','U','_','R','0','_','O','K',' '
    dw 'N','=','8',' ','S','E','M','=','M','O','D','2','P','3','2',13,10,0
msg_e201:
    dw 'N','G','_','N','O','V','A','_','C','P','U','_','R','0','_','E','2','0','1','_','R','E','S','U','L','T',13,10,0
msg_e202:
    dw 'N','G','_','N','O','V','A','_','C','P','U','_','R','0','_','E','2','0','2','_','G','U','A','R','D',13,10,0
bootstrap_external_asm_tag:
    db 'BOOTSTRAP_EXTERNAL_ASM',0
    align 8, db 0
reloc_anchor:
    dq IMAGE_BASE + entry
rdata_end:
    times 0x0200 - ($ - $$) db 0

SECTION .data start=0x0800 vstart=0x3000 align=16
data_start:
guard_lo:
    dd 0xC0DEC0DE
out_vec:
    times VECTOR_COUNT dd 0xA5A5A5A5
guard_hi:
    dd 0xDEC0ADDE
data_end:
    times 0x0200 - ($ - $$) db 0

SECTION .reloc start=0x0A00 vstart=0x4000 align=4
reloc_start:
    dd 0x2000
    dd 12
    dw 0xA000 | (reloc_anchor - rdata_start)
    dw 0
reloc_end:
    times 0x0200 - ($ - $$) db 0
