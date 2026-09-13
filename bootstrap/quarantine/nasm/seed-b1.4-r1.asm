; BOOTSTRAP_EXTERNAL_ASM
; Quarantaine temporaire autorisée pour le tracer B1.4 uniquement.
; Commande de matérialisation quarantainée :
; nasm -f bin bootstrap/quarantine/nasm/seed-b1.4-r1.asm -o bootstrap/quarantine/nasm/out/seed-b1.4-r1.efi

BITS 64
ORG 0

%define IMAGE_BASE              0x0000000140000000
%define EFI_SUCCESS             0
%define EFI_LOAD_ERROR          0x8000000000000001
%define EFI_DEVICE_ERROR        0x8000000000000007
%define EFI_OUT_OF_RESOURCES    0x8000000000000009
%define EFI_FILE_MODE_READ      1
%define EFI_FILE_DIRECTORY      0x10
%define EfiLoaderData           2

%define OWN_WORK                0x01
%define OWN_SRC                 0x02
%define OWN_OUT                 0x04
%define OWN_ROOT                0x08
%define OWN_SOURCE              0x10

%define F_OPEN_ARG5             0x20
%define F_LOADED                0x28
%define F_SIMPLEFS              0x30
%define F_ROOT                  0x38
%define F_SOURCE                0x40
%define F_INFO_SIZE             0x48
%define F_READ_SIZE             0x50
%define F_WORK_RAW              0x58
%define F_WORK                  0x60
%define F_SRC_RAW               0x68
%define F_SRC                   0x70
%define F_OUT_RAW               0x78
%define F_OUT                   0x80
%define F_SOURCE_LEN            0x88
%define F_DIAG                  0x90
%define F_LINE                  0x98
%define F_COLUMN                0xA0
%define F_OFFSET                0xA8
%define F_EXIT_STATUS           0xB0
%define F_LINE_ATTEMPTED        0xB8
%define F_FREE_ERROR            0xC0
%define F_LINES_DONE            0xC8
%define F_REMAINING             0xD0
%define F_READ_STATUS           0xD8
%define F_OWNERSHIP             0xE0
%define FRAME_SIZE              0xE8

; Après Close(source), ces slots UEFI morts deviennent l'état sémantique B1.3.
%define F_SEM_INDEX             F_INFO_SIZE
%define F_SEM_STAGE             F_READ_SIZE
%define F_SEM_BEGIN             F_LOADED
%define F_SEM_START             F_REMAINING
%define F_SEM_LINE              F_READ_STATUS

%macro SET_ZERO_POSITION 0
    mov qword [rbp + F_LINE], 0
    mov qword [rbp + F_COLUMN], 0
    mov qword [rbp + F_OFFSET], 0
%endmacro

%macro ALLOC_GUARDED 6
    ; raw-slot, payload-slot, raw-size, PRE, POST, ownership-bit
    mov qword [rbp + %1], 0
    mov rax, [r14 + 0x40]
    mov ecx, EfiLoaderData
    mov edx, %3
    lea r8, [rbp + %1]
    call rax
    test rax, rax
    js fail_alloc
    mov rax, [rbp + %1]
    test rax, rax
    jz fail_alloc
    or byte [rbp + F_OWNERSHIP], %6
    mov rcx, rax
    add rcx, 0x10
    jc fail_e603
    mov [rbp + %2], rcx
    mov r10, rax
    add r10, %3                    ; borne one-past de l'allocation brute
    jc fail_e603
    mov rcx, [rbp + %1]
    lea rdx, [rel %4]
    mov r8d, 16
    call copy_n
    mov rcx, [rbp + %1]
    add rcx, %3
    jc fail_e603
    sub rcx, 0x10
    lea rdx, [rel %5]
    mov r8d, 16
    call copy_n
%endmacro

%macro FREE_OWNED 3
    ; raw-slot, ownership-bit, continuation-label
    test byte [rbp + F_OWNERSHIP], %2
    jz %3
    mov rcx, [rbp + %1]
    test rcx, rcx
    jnz %%call
    mov rax, EFI_DEVICE_ERROR
    call record_free_error
    jmp %3
%%call:
    mov rax, [r14 + 0x48]
    call rax
    test rax, rax
    jnz %%failed
    mov qword [rbp + %1], 0
    and byte [rbp + F_OWNERSHIP], (0xFF ^ %2)
    jmp %3
%%failed:
    call record_free_error
    jmp %3
%endmacro

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
    dd 0x1200
    dd 0x0800
    dd 0
    dd entry
    dd 0x1000
    dq IMAGE_BASE
    dd 0x1000
    dd 0x0200
    dw 0,0,0,0,0,0
    dd 0
    dd 0x6000
    dd 0x0400
    dd 0
    dw 10
    dw 0x0140
    dq 0x100000,0x1000,0x100000,0x1000
    dd 0
    dd 16

    ; 16 data directories; seule Base Relocation (index 5) est non nulle.
    times 5 dq 0
    dd 0x5000, 0x000C
    times 10 dq 0

    ; Table des quatre sections, offset 0x188.
    db '.text',0,0,0
    dd text_end - text_start
    dd 0x1000
    dd 0x1200
    dd 0x0400
    dd 0,0
    dw 0,0
    dd 0x60000020

    db '.rdata',0,0
    dd rdata_end - rdata_start
    dd 0x3000
    dd 0x0400
    dd 0x1600
    dd 0,0
    dw 0,0
    dd 0x40000040

    db '.data',0,0,0
    dd data_end - data_start
    dd 0x4000
    dd 0x0200
    dd 0x1A00
    dd 0,0
    dw 0,0
    dd 0xC0000040

    db '.reloc',0,0
    dd reloc_end - reloc_start
    dd 0x5000
    dd 0x0200
    dd 0x1C00
    dd 0,0
    dw 0,0
    dd 0x42000040

    times 0x400 - ($ - $$) db 0

SECTION .text start=0x0400 vstart=0x1000 align=16
text_start:
entry:
    push rbx
    push rbp
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    sub rsp, FRAME_SIZE
    mov rbp, rsp
    mov r12, rcx                    ; ImageHandle
    mov r13, rdx                    ; SystemTable

    lea rdi, [rbp + 0x28]
    xor eax, eax
    mov ecx, 24                     ; zéro jusqu'au masque ownership inclus
.zero_frame:
    mov [rdi], rax
    add rdi, 8
    dec ecx
    jnz .zero_frame

    test r13, r13
    jz fail_e001_load
    mov r15, [r13 + 0x40]           ; ConOut
    mov r14, [r13 + 0x60]           ; BootServices
    test r15, r15
    jz fail_e001_load
    test r14, r14
    jz fail_e001_load
    cmp qword [r14 + 0x40], 0       ; AllocatePool
    je fail_e001_load
    cmp qword [r14 + 0x48], 0       ; FreePool
    je fail_e001_load
    cmp qword [r14 + 0x98], 0       ; HandleProtocol
    je fail_e001_load
    cmp qword [r14 + 0xD8], 0       ; Exit
    je fail_e001_load

    ALLOC_GUARDED F_WORK_RAW, F_WORK, 0x1D020, work_pre, work_post, OWN_WORK

    ; WORK payload doit être nul avant toute utilisation.
    mov rdi, [rbp + F_WORK]
    xor eax, eax
    mov ecx, 0x3A00
.zero_work:
    mov [rdi], rax
    add rdi, 8
    dec ecx
    jnz .zero_work

    ALLOC_GUARDED F_SRC_RAW, F_SRC, 0x10020, src_pre, src_post, OWN_SRC
    ALLOC_GUARDED F_OUT_RAW, F_OUT, 0x10020, out_pre, out_post, OWN_OUT

    call verify_guards              ; checkpoint post-initialisation
    test eax, eax
    jnz fail_e603

    ; LoadedImage -> DeviceHandle -> SimpleFS.
    mov qword [rbp + F_LOADED], 0
    mov rcx, r12
    lea rdx, [rel loaded_image_guid]
    lea r8, [rbp + F_LOADED]
    mov rax, [r14 + 0x98]
    call rax
    test rax, rax
    js fail_e001_firmware
    mov rax, [rbp + F_LOADED]
    test rax, rax
    jz fail_e001_load
    mov rcx, [rax + 0x18]
    test rcx, rcx
    jz fail_e001_load

    mov qword [rbp + F_SIMPLEFS], 0
    lea rdx, [rel simple_fs_guid]
    lea r8, [rbp + F_SIMPLEFS]
    mov rax, [r14 + 0x98]
    call rax
    test rax, rax
    js fail_e001_firmware
    mov rcx, [rbp + F_SIMPLEFS]
    test rcx, rcx
    jz fail_e001_load

    mov rax, [rcx + 0x08]           ; OpenVolume
    test rax, rax
    jz fail_e001_load
    mov qword [rbp + F_ROOT], 0
    lea rdx, [rbp + F_ROOT]
    call rax
    test rax, rax
    js fail_e001_firmware
    mov rbx, [rbp + F_ROOT]
    test rbx, rbx
    jz fail_e001_load
    or byte [rbp + F_OWNERSHIP], OWN_ROOT

    ; L'unique Open fichier cible la source en READ, jamais NTASM1.EFI.
    mov rax, [rbx + 0x08]
    test rax, rax
    jz fail_e001_load
    mov qword [rbp + F_SOURCE], 0
    mov rcx, rbx
    lea rdx, [rbp + F_SOURCE]
    lea r8, [rel source_name]
    mov r9d, EFI_FILE_MODE_READ
    mov qword [rbp + F_OPEN_ARG5], 0
    call rax
    test rax, rax
    js fail_e001_firmware
    mov rsi, [rbp + F_SOURCE]
    test rsi, rsi
    jz fail_e001_load
    or byte [rbp + F_OWNERSHIP], OWN_SOURCE

    ; GetInfo avant Read : buffer WORK+1CA00, Attribute +48, DIRECTORY=10h.
    mov rax, [rsi + 0x40]
    test rax, rax
    jz fail_e002_device
    mov qword [rbp + F_INFO_SIZE], 0x200
    mov rcx, rsi
    lea rdx, [rel file_info_guid]
    lea r8, [rbp + F_INFO_SIZE]
    mov r9, [rbp + F_WORK]
    add r9, 0x1CA00
    jc fail_e603
    call rax
    test rax, rax
    js fail_e002_firmware
    mov rax, [rbp + F_INFO_SIZE]
    cmp rax, 0x50
    jb fail_e002_device
    cmp rax, 0x200
    ja fail_e002_device
    mov rcx, [rbp + F_WORK]
    add rcx, 0x1CA48
    jc fail_e603
    mov rdx, [rcx]
    test dl, EFI_FILE_DIRECTORY
    jnz fail_e002_device

read_loop:
    mov rcx, [rbp + F_SOURCE_LEN]
    cmp rcx, 0x10000
    jae fail_e500_zero
    mov eax, 0x10000
    sub rax, rcx
    mov [rbp + F_REMAINING], rax
    mov [rbp + F_READ_SIZE], rax
    mov rax, [rsi + 0x20]
    test rax, rax
    jz fail_e002_device
    mov rcx, rsi
    lea rdx, [rbp + F_READ_SIZE]
    mov r8, [rbp + F_SRC]
    add r8, [rbp + F_SOURCE_LEN]
    jc fail_e603
    call rax                         ; unique site Read
    mov [rbp + F_READ_STATUS], rax

    call verify_guards               ; checkpoint après chaque Read
    test eax, eax
    jnz fail_e603
    mov rax, [rbp + F_READ_STATUS]
    test rax, rax
    js fail_e002_firmware

    mov rax, [rbp + F_READ_SIZE]
    cmp rax, [rbp + F_REMAINING]
    ja fail_e002_device
    test rax, rax
    jz read_eof
    add rax, [rbp + F_SOURCE_LEN]
    cmp rax, 0x10000
    jae fail_e500_zero
    mov [rbp + F_SOURCE_LEN], rax
    jmp read_loop

read_eof:
    cmp qword [rbp + F_SOURCE_LEN], 0
    je fail_e100_zero

    ; Close source : pointer/ownership effacés uniquement après succès.
    mov rax, [rsi + 0x10]
    test rax, rax
    jz fail_e002_device
    mov rcx, rsi
    call rax
    test rax, rax
    js fail_e002_firmware
    mov qword [rbp + F_SOURCE], 0
    and byte [rbp + F_OWNERSHIP], (0xFF ^ OWN_SOURCE)
    xor esi, esi

    ; Scanner physique : cursor=r8, ligne=r9, start=r10, longueur=r11.
    mov rdi, [rbp + F_SRC]
    xor r8d, r8d
    mov r9d, 1
    xor r10d, r10d
    xor r11d, r11d
    mov qword [rbp + F_LINES_DONE], 0

scan_loop:
    cmp r8, [rbp + F_SOURCE_LEN]
    jae scan_eof
    cmp dword [rbp + F_LINES_DONE], 4096
    je scan_line_over

    test r8, r8
    jnz scan_load
    mov rax, [rbp + F_SOURCE_LEN]
    sub rax, r8
    cmp rax, 3                       ; BOM lookahead seulement si remaining>=3
    jb scan_load
    cmp byte [rdi], 0xEF
    jne scan_load
    cmp byte [rdi + 1], 0xBB
    jne scan_load
    cmp byte [rdi + 2], 0xBF
    je scan_bad

scan_load:
    movzx eax, byte [rdi + r8]
    cmp al, 0x0D
    je scan_cr
    cmp al, 0x0A
    je scan_eol1
    cmp al, 0x09
    je scan_char
    cmp al, 0x20
    jb scan_bad
    cmp al, 0x7E
    ja scan_bad

scan_char:
    cmp r11d, 255
    je scan_line_long
    inc r11
    inc r8
    jmp scan_loop

scan_cr:
    lea rax, [r8 + 1]
    cmp rax, [rbp + F_SOURCE_LEN]
    jae scan_bad
    cmp byte [rdi + r8 + 1], 0x0A
    jne scan_bad
    mov ecx, 2
    jmp scan_eol

scan_eol1:
    mov ecx, 1

scan_eol:
    mov eax, [rbp + F_LINES_DONE]
    mov rdx, [rbp + F_WORK]
    add rdx, 0x14400
    jc fail_e603
    imul rsi, rax, 8
    jo fail_e603
    add rsi, rdx
    jc fail_e603
    mov edx, r10d
    mov eax, r11d
    shl rax, 32
    or rdx, rax
    movzx eax, cl
    shl rax, 48
    or rdx, rax
    mov [rsi], rdx
    inc dword [rbp + F_LINES_DONE]
    add r8, rcx
    mov r10, r8
    xor r11d, r11d
    inc r9d
    jmp scan_loop

scan_bad:
    mov edi, 100
    mov rax, r9
    lea rdx, [r11 + 1]
    mov rcx, r8
    jmp fail_positioned

scan_line_long:
    mov edi, 101
    mov rax, r9
    mov edx, 0x100
    mov rcx, r8
    jmp fail_positioned

scan_line_over:
    mov edi, 500
    mov rax, r9
    mov edx, 1
    mov rcx, r8
    jmp fail_positioned

scan_eof:
    test r11, r11
    jnz scan_eof_bad

    call verify_guards              ; checkpoint post-scan
    test eax, eax
    jnz fail_e603

    mov qword [rbp + F_SEM_INDEX], 0
    mov qword [rbp + F_SEM_STAGE], 0

semantic_next_record:
    mov rax, [rbp + F_SEM_INDEX]
    cmp eax, dword [rbp + F_LINES_DONE]
    jae semantic_eof
    mov rcx, rax
    inc rcx
    mov [rbp + F_SEM_LINE], rcx
    inc qword [rbp + F_SEM_INDEX]

    mov rdx, [rbp + F_WORK]
    add rdx, 0x14400
    jc fail_e603
    imul rax, rax, 8
    jo fail_e603
    add rdx, rax
    jc fail_e603
    mov rax, [rdx]
    mov r10d, eax                    ; offset physique de la ligne
    shr rax, 32
    and eax, 0xFFFF
    mov r11, rax                     ; longueur hors EOL
    mov [rbp + F_SEM_START], r10
    mov rax, r10
    add rax, r11
    jc fail_e603
    cmp rax, [rbp + F_SOURCE_LEN]
    ja fail_e603
    mov rdi, [rbp + F_SRC]
    lea rdx, [rdi + r10]             ; début de ligne borné

    xor r8d, r8d                     ; début logique après leading WS
.trim_leading:
    cmp r8, r11
    jae semantic_next_record
    mov al, [rdx + r8]
    cmp al, 0x20
    je .leading_one
    cmp al, 0x09
    jne .scan_comment
.leading_one:
    inc r8
    jmp .trim_leading

    ; B1.3 n'admet aucune chaîne dans le préambule : le premier ';' coupe.
    ; Tout guillemet placé avant lui reste dans la tranche et sera refusé E200.
.scan_comment:
    mov r9, r11                     ; fin logique par défaut
    mov rsi, r8
.comment_loop:
    cmp rsi, r11
    jae .trim_trailing
    mov al, [rdx + rsi]
    cmp al, ';'
    je .comment_found
    inc rsi
    jmp .comment_loop
.comment_found:
    mov r9, rsi

.trim_trailing:
    cmp r9, r8
    jbe semantic_next_record
    mov al, [rdx + r9 - 1]
    cmp al, 0x20
    je .trailing_one
    cmp al, 0x09
    jne .semantic_line
.trailing_one:
    dec r9
    jmp .trim_trailing

.semantic_line:
    mov [rbp + F_SEM_BEGIN], r8
    mov rcx, rdx                     ; pointeur ligne
    mov rdx, r8                      ; début logique
    mov r8, r9                       ; fin logique exclusive
    mov r9, [rbp + F_SEM_STAGE]      ; 0=format, 1=abi, 2=probe const
    cmp r9d, 2
    je semantic_probe_const
    call match_preamble_line
    test rax, rax
    jnz semantic_e200
    inc qword [rbp + F_SEM_STAGE]
    jmp semantic_next_record

semantic_probe_const:
    call match_const_prefix
    test rax, rax
    jnz semantic_const_error
    jmp cleanup

semantic_eof:
    cmp qword [rbp + F_SEM_STAGE], 2
    jae cleanup                     ; zéro constante est valide à B1.4
    cmp qword [rbp + F_SEM_STAGE], 0
    je fail_e200                    ; aucune ligne format : compatibilité B1.1
    mov edi, 200
    mov rax, [rbp + F_LINES_DONE]
    inc rax
    mov edx, 1
    mov rcx, [rbp + F_SOURCE_LEN]
    jmp fail_positioned

semantic_e200:
    cmp qword [rbp + F_SEM_STAGE], 0
    jne semantic_e200_precise
    mov r11, [rbp + F_SEM_BEGIN]
    lea rdx, [r11 + 1]
    add r11, [rbp + F_SEM_START]
    jc fail_e603
    mov rcx, r11
    mov rax, [rbp + F_SEM_LINE]
    mov edi, 200
    jmp fail_positioned

semantic_const_error:
    mov r10d, edx                   ; E101 ou E201
    mov r11, rax                    ; colonne physique = index + 1
    dec r11
    add r11, [rbp + F_SEM_START]
    jc fail_e603
    mov rdx, rax
    mov rcx, r11
    mov rax, [rbp + F_SEM_LINE]
    mov edi, r10d
    jmp fail_positioned
semantic_e200_precise:
    mov r11, rax                    ; colonne = index fautif + 1
    dec r11                         ; index fautif dans la ligne physique
    add r11, [rbp + F_SEM_START]
    jc fail_e603
    mov rdx, rax
    mov rcx, r11
    mov rax, [rbp + F_SEM_LINE]
    mov edi, 200
    jmp fail_positioned

scan_eof_bad:
    mov edi, 100
    mov rax, r9
    lea rdx, [r11 + 1]
    mov rcx, [rbp + F_SOURCE_LEN]
    jmp fail_positioned

fail_alloc:
    mov edi, 500
    mov rax, EFI_OUT_OF_RESOURCES
    jmp fail_zero_with_status

fail_e001_load:
    mov edi, 1
    mov rax, EFI_LOAD_ERROR
    jmp fail_zero_with_status

fail_e001_firmware:
    mov edi, 1
    jmp fail_zero_with_status

fail_e002_device:
    mov rax, EFI_DEVICE_ERROR
fail_e002_firmware:
    mov edi, 2
    jmp fail_zero_with_status

fail_e100_zero:
    mov edi, 100
    mov rax, EFI_LOAD_ERROR
    jmp fail_zero_with_status

fail_e200:
    mov edi, 200
    mov eax, 1
    mov edx, 1
    xor ecx, ecx
    jmp fail_positioned

fail_e500_zero:
    mov edi, 500
    mov rax, EFI_LOAD_ERROR

fail_zero_with_status:
    mov [rbp + F_DIAG], rdi
    SET_ZERO_POSITION
    mov [rbp + F_EXIT_STATUS], rax
    jmp cleanup

fail_positioned:
    mov [rbp + F_DIAG], rdi
    mov [rbp + F_LINE], rax
    mov [rbp + F_COLUMN], rdx
    mov [rbp + F_OFFSET], rcx
    mov rax, EFI_LOAD_ERROR
    mov [rbp + F_EXIT_STATUS], rax
    jmp cleanup

fail_e603:
    mov qword [rbp + F_DIAG], 603
    SET_ZERO_POSITION
    mov rax, EFI_LOAD_ERROR
    mov [rbp + F_EXIT_STATUS], rax

cleanup:
    ; Close source si ownership présent. Aucun clear avant succès.
    test byte [rbp + F_OWNERSHIP], OWN_SOURCE
    jz cleanup_root
    mov rcx, [rbp + F_SOURCE]
    test rcx, rcx
    jz cleanup_source_device
    mov rax, [rcx + 0x10]
    test rax, rax
    jz cleanup_source_device
    call rax
    test rax, rax
    js cleanup_source_error
    mov qword [rbp + F_SOURCE], 0
    and byte [rbp + F_OWNERSHIP], (0xFF ^ OWN_SOURCE)
    jmp cleanup_root
cleanup_source_device:
    mov rax, EFI_DEVICE_ERROR
cleanup_source_error:
    call record_close_error

cleanup_root:
    test byte [rbp + F_OWNERSHIP], OWN_ROOT
    jz cleanup_guards
    mov rcx, [rbp + F_ROOT]
    test rcx, rcx
    jz cleanup_root_device
    mov rax, [rcx + 0x10]
    test rax, rax
    jz cleanup_root_device
    call rax
    test rax, rax
    js cleanup_root_error
    mov qword [rbp + F_ROOT], 0
    and byte [rbp + F_OWNERSHIP], (0xFF ^ OWN_ROOT)
    jmp cleanup_guards
cleanup_root_device:
    mov rax, EFI_DEVICE_ERROR
cleanup_root_error:
    call record_close_error

cleanup_guards:
    call verify_guards              ; checkpoint pré-FreePool
    test eax, eax
    jz report_before_free
    mov qword [rbp + F_DIAG], 603   ; override absolu
    SET_ZERO_POSITION
    mov rax, EFI_LOAD_ERROR
    mov [rbp + F_EXIT_STATUS], rax

report_before_free:
    mov rax, [rbp + F_DIAG]
    test rax, rax
    jz free_out
    mov qword [rbp + F_LINE_ATTEMPTED], 1
    cmp eax, 603
    je .e603_static
    cmp qword [rbp + F_WORK], 0
    je .without_work
    call format_base
    test rax, rax
    jz fail_e603
    mov rbx, rax
    mov rcx, [rbp + F_LINE]
    lea rdx, [rbx + 0x16]
    mov r8d, 4
    call write_hex
    mov rcx, [rbp + F_COLUMN]
    lea rdx, [rbx + 0x22]
    mov r8d, 4
    call write_hex
    mov rcx, [rbp + F_OFFSET]
    lea rdx, [rbx + 0x2E]
    mov r8d, 8
    call write_hex
    mov rdx, rbx
    jmp .output
.e603_static:
    lea rdx, [rel diagnostic_e603]
    jmp .output
.without_work:
    cmp eax, 1
    je .e001_static
    cmp eax, 2
    je .e002_static
    lea rdx, [rel diagnostic_e500]
    jmp .output
.e001_static:
    lea rdx, [rel diagnostic_e001]
    jmp .output
.e002_static:
    lea rdx, [rel diagnostic_e002]
.output:
    call output_string_if_present

free_out:
    FREE_OWNED F_OUT_RAW, OWN_OUT, free_src
free_src:
    FREE_OWNED F_SRC_RAW, OWN_SRC, free_work
free_work:
    FREE_OWNED F_WORK_RAW, OWN_WORK, after_free

after_free:
    cmp qword [rbp + F_LINE_ATTEMPTED], 0
    jne do_exit
    mov rax, [rbp + F_FREE_ERROR]
    test rax, rax
    jz report_success
    mov qword [rbp + F_DIAG], 604
    mov [rbp + F_EXIT_STATUS], rax
    mov qword [rbp + F_LINE_ATTEMPTED], 1
    lea rdx, [rel diagnostic_e604]
    call output_string_if_present
    jmp do_exit

report_success:
    mov qword [rbp + F_EXIT_STATUS], EFI_SUCCESS
    lea rdx, [rel success_b14]
    call output_string_if_present

do_exit:
    test r14, r14
    jz return_status
    mov rax, [r14 + 0xD8]
    test rax, rax
    jz return_status
    mov rcx, r12
    mov rdx, [rbp + F_EXIT_STATUS]
    xor r8d, r8d
    xor r9d, r9d
    call rax
    jmp epilogue

return_status:
    mov rax, [rbp + F_EXIT_STATUS]

epilogue:
    add rsp, FRAME_SIZE
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    ret

; --- Helpers privés --------------------------------------------------------

copy_n:
    test r8, r8
    jz .done
.loop:
    mov al, [rdx]
    mov [rcx], al
    inc rdx
    inc rcx
    dec r8
    jnz .loop
.done:
    ret

memcmp_n:
    xor eax, eax
    test r8, r8
    jz .done
.loop:
    mov r9b, [rcx]
    cmp r9b, [rdx]
    jne .bad
    inc rcx
    inc rdx
    dec r8
    jnz .loop
.done:
    ret
.bad:
    mov eax, 1
    ret

; rcx=line, rdx=begin, r8=end, r9=stage. Retour 0 ou index fautif + 1.
match_preamble_line:
    test r9d, r9d
    jnz .abi_first
    lea r10, [rel format_kw]
    mov r11d, 6
    jmp .token_one
.abi_first:
    lea r10, [rel abi_kw]
    mov r11d, 3

.token_one:
    test r11d, r11d
    jz .require_separator
    cmp rdx, r8
    jae .fail
    mov al, [rcx + rdx]
    cmp al, [r10]
    jne .fail
    inc rdx
    inc r10
    dec r11d
    jmp .token_one

.require_separator:
    cmp rdx, r8
    jae .fail
    mov al, [rcx + rdx]
    cmp al, 0x20
    je .skip_separator
    cmp al, 0x09
    jne .fail
.skip_separator:
    inc rdx
    cmp rdx, r8
    jae .fail
    mov al, [rcx + rdx]
    cmp al, 0x20
    je .skip_separator
    cmp al, 0x09
    je .skip_separator

    test r9d, r9d
    jnz .abi_second
    lea r10, [rel pe64_kw]
    mov r11d, 4
    jmp .token_two
.abi_second:
    lea r10, [rel efi_x64_kw]
    mov r11d, 7

.token_two:
    test r11d, r11d
    jz .trailing
    cmp rdx, r8
    jae .fail
    mov al, [rcx + rdx]
    cmp al, [r10]
    jne .fail
    inc rdx
    inc r10
    dec r11d
    jmp .token_two

.trailing:
    cmp rdx, r8
    jae .ok
    mov al, [rcx + rdx]
    cmp al, 0x20
    je .trailing_one
    cmp al, 0x09
    jne .fail
.trailing_one:
    inc rdx
    jmp .trailing
.ok:
    xor eax, eax
    ret
.fail:
    lea rax, [rdx + 1]
    ret

; rcx=line, rdx=begin, r8=end. Retour 0 ou index fautif+1; edx=101/201.
match_const_prefix:
    mov rax, r8
    sub rax, rdx
    cmp rax, 5
    jb .not_const
    cmp byte [rcx + rdx], 'c'
    jne .not_const
    cmp byte [rcx + rdx + 1], 'o'
    jne .not_const
    cmp byte [rcx + rdx + 2], 'n'
    jne .not_const
    cmp byte [rcx + rdx + 3], 's'
    jne .not_const
    cmp byte [rcx + rdx + 4], 't'
    jne .not_const
    add rdx, 5
    cmp rdx, r8
    jae .e201
    mov al, [rcx + rdx]
    cmp al, 0x20
    je .skip_keyword_ws
    cmp al, 0x09
    je .skip_keyword_ws
    ; Une continuation d'identifiant forme `constA`/`const1`/`const_` et reste
    ; hors périmètre. Toute ponctuation après le mot-clé exact est E201.
    cmp al, '_'
    je .not_const
    cmp al, '0'
    jb .check_const_upper
    cmp al, '9'
    jbe .not_const
.check_const_upper:
    cmp al, 'A'
    jb .check_const_lower
    cmp al, 'Z'
    jbe .not_const
.check_const_lower:
    cmp al, 'a'
    jb .e201
    cmp al, 'z'
    jbe .not_const
    jmp .e201
.skip_keyword_ws:
    inc rdx
    cmp rdx, r8
    jae .e201
    mov al, [rcx + rdx]
    cmp al, 0x20
    je .skip_keyword_ws
    cmp al, 0x09
    je .skip_keyword_ws

    ; Premier caractère : lettre ASCII ou underscore.
    cmp al, '_'
    je .identifier_start
    cmp al, 'A'
    jb .check_first_lower
    cmp al, 'Z'
    jbe .identifier_start
.check_first_lower:
    cmp al, 'a'
    jb .e201
    cmp al, 'z'
    ja .e201

.identifier_start:
    mov r9, rdx                    ; début identifiant
    xor r10d, r10d                 ; longueur
.identifier_valid:
    cmp r10d, 31
    jae .e101                      ; l'octet courant serait le 32e
    inc r10d
    inc rdx
.identifier_loop:
    cmp rdx, r8
    jae .identifier_done
    mov al, [rcx + rdx]
    cmp al, '_'
    je .identifier_valid
    cmp al, '0'
    jb .check_upper
    cmp al, '9'
    jbe .identifier_valid
.check_upper:
    cmp al, 'A'
    jb .check_lower
    cmp al, 'Z'
    jbe .identifier_valid
.check_lower:
    cmp al, 'a'
    jb .identifier_done
    cmp al, 'z'
    jbe .identifier_valid

.identifier_done:
    ; `const` est le mot-clé réservé canonique refusé comme identifiant à B1.4.
    cmp r10d, 5
    jne .after_keyword_check
    cmp byte [rcx + r9], 'c'
    jne .after_keyword_check
    cmp byte [rcx + r9 + 1], 'o'
    jne .after_keyword_check
    cmp byte [rcx + r9 + 2], 'n'
    jne .after_keyword_check
    cmp byte [rcx + r9 + 3], 's'
    jne .after_keyword_check
    cmp byte [rcx + r9 + 4], 't'
    jne .after_keyword_check
    mov rdx, r9
    jmp .e201

.after_keyword_check:
    cmp rdx, r8
    jae .e201
    mov al, [rcx + rdx]
    cmp al, 0x20
    je .skip_ident_ws
    cmp al, 0x09
    jne .expect_equal
.skip_ident_ws:
    inc rdx
    cmp rdx, r8
    jae .e201
    mov al, [rcx + rdx]
    cmp al, 0x20
    je .skip_ident_ws
    cmp al, 0x09
    je .skip_ident_ws
.expect_equal:
    cmp byte [rcx + rdx], '='
    jne .e201
    xor eax, eax                    ; suffixe après '=' hors périmètre B1.4
    xor edx, edx
    ret

.not_const:
    xor eax, eax                    ; zéro constante / suffixe futur accepté
    xor edx, edx
    ret
.e101:
    lea rax, [rdx + 1]
    mov edx, 101
    ret
.e201:
    lea rax, [rdx + 1]
    mov edx, 201
    ret

verify_guards:
    sub rsp, 0x28                   ; alignement + shadow pour memcmp_n
    test byte [rbp + F_OWNERSHIP], OWN_WORK
    jz .src
    mov rcx, [rbp + F_WORK_RAW]
    test rcx, rcx
    jz .bad
    lea rdx, [rel work_pre]
    mov r8d, 16
    call memcmp_n
    test eax, eax
    jnz .bad
    mov rcx, [rbp + F_WORK_RAW]
    add rcx, 0x1D010
    jc .bad
    lea rdx, [rel work_post]
    mov r8d, 16
    call memcmp_n
    test eax, eax
    jnz .bad
.src:
    test byte [rbp + F_OWNERSHIP], OWN_SRC
    jz .out
    mov rcx, [rbp + F_SRC_RAW]
    test rcx, rcx
    jz .bad
    lea rdx, [rel src_pre]
    mov r8d, 16
    call memcmp_n
    test eax, eax
    jnz .bad
    mov rcx, [rbp + F_SRC_RAW]
    add rcx, 0x10010
    jc .bad
    lea rdx, [rel src_post]
    mov r8d, 16
    call memcmp_n
    test eax, eax
    jnz .bad
.out:
    test byte [rbp + F_OWNERSHIP], OWN_OUT
    jz .ok
    mov rcx, [rbp + F_OUT_RAW]
    test rcx, rcx
    jz .bad
    lea rdx, [rel out_pre]
    mov r8d, 16
    call memcmp_n
    test eax, eax
    jnz .bad
    mov rcx, [rbp + F_OUT_RAW]
    add rcx, 0x10010
    jc .bad
    lea rdx, [rel out_post]
    mov r8d, 16
    call memcmp_n
    test eax, eax
    jnz .bad
.ok:
    xor eax, eax
    add rsp, 0x28
    ret
.bad:
    mov eax, 1
    add rsp, 0x28
    ret

record_close_error:
    cmp qword [rbp + F_DIAG], 0
    jne .done
    mov qword [rbp + F_DIAG], 2
    SET_ZERO_POSITION
    test rax, rax
    js .store
    mov rax, EFI_DEVICE_ERROR
.store:
    mov [rbp + F_EXIT_STATUS], rax
.done:
    ret

record_free_error:
    test rax, rax
    jz .done
    cmp qword [rbp + F_FREE_ERROR], 0
    jne .done
    test rax, rax
    js .store
    mov rax, EFI_DEVICE_ERROR
.store:
    mov [rbp + F_FREE_ERROR], rax
.done:
    ret

format_base:
    mov r11, [rbp + F_WORK]
    add r11, 0x1C900
    jc .bad
    mov r10, r11
    lea rdx, [rel diagnostic_template]
    mov r8d, 68
.copy:
    mov al, [rdx]
    mov [r10], al
    inc rdx
    inc r10
    dec r8
    jnz .copy
    mov rax, [rbp + F_DIAG]
    xor edx, edx
    mov ecx, 100
    div rcx
    add eax, '0'
    mov [r11 + 0x0C], ax
    mov rax, rdx
    xor edx, edx
    mov ecx, 10
    div rcx
    add eax, '0'
    mov [r11 + 0x0E], ax
    add edx, '0'
    mov [r11 + 0x10], dx
    mov rax, r11
    ret
.bad:
    xor eax, eax
    ret

write_hex:
    mov rax, rcx
    mov r9d, r8d
    dec r9d
    lea rdx, [rdx + r9*2]
.loop:
    mov r10, rax
    and r10d, 0x0F
    cmp r10d, 9
    jbe .digit
    add r10d, 'A' - 10
    jmp .store
.digit:
    add r10d, '0'
.store:
    mov [rdx], r10w
    shr rax, 4
    sub rdx, 2
    dec r8d
    jnz .loop
    ret

output_string_if_present:
    sub rsp, 0x28                   ; alignement + shadow pour OutputString
    test r15, r15
    jz .done
    mov rax, [r15 + 0x08]
    test rax, rax
    jz .done
    mov rcx, r15
    call rax
.done:
    add rsp, 0x28
    ret

text_end:
    %if (text_end - text_start) > 0x1200
        %error "B1.4 .text depasse 0x1200"
    %endif
    times 0x1200 - ($ - $$) db 0

SECTION .rdata start=0x1600 vstart=0x3000 align=16
rdata_start:
loaded_image_guid:
    db 0xA1,0x31,0x1B,0x5B,0x62,0x95,0xD2,0x11,0x8E,0x3F,0x00,0xA0,0xC9,0x69,0x72,0x3B
simple_fs_guid:
    db 0x22,0x5B,0x4E,0x96,0x59,0x64,0xD2,0x11,0x8E,0x39,0x00,0xA0,0xC9,0x69,0x72,0x3B
file_info_guid:
    db 0x92,0x6E,0x57,0x09,0x3F,0x6D,0xD2,0x11,0x8E,0x39,0x00,0xA0,0xC9,0x69,0x72,0x3B
source_name:
    dw '\','N','T','A','S','M','1','.','N','T','A','S','M',0

    times 0x50 - ($ - $$) db 0
work_pre:  db 0xA5,0x5A,0xC3,0x3C,0x96,0x69,0xF0,0x0F,0x55,0xAA,0x33,0xCC,0x78,0x87,0xE1,0x1E
work_post: db 0x1E,0xE1,0x87,0x78,0xCC,0x33,0xAA,0x55,0x0F,0xF0,0x69,0x96,0x3C,0xC3,0x5A,0xA5
src_pre:   db 0x99,0x66,0xFF,0x00,0xAA,0x55,0xCC,0x33,0x69,0x96,0x0F,0xF0,0x44,0xBB,0xDD,0x22
src_post:  db 0x22,0xDD,0xBB,0x44,0xF0,0x0F,0x96,0x69,0x33,0xCC,0x55,0xAA,0x00,0xFF,0x66,0x99
out_pre:   db 0x66,0x99,0x00,0xFF,0x55,0xAA,0x33,0xCC,0x96,0x69,0xF0,0x0F,0xBB,0x44,0x22,0xDD
out_post:  db 0xDD,0x22,0x44,0xBB,0x0F,0xF0,0x69,0x96,0xCC,0x33,0xAA,0x55,0xFF,0x00,0x99,0x66

format_kw:
    db 'format'
pe64_kw:
    db 'pe64'

    times 0xC0 - ($ - $$) db 0
diagnostic_template:
    dw 'N','G','S','0',' ','E','0','0','0',' ','L','0','0','0','0',' '
    dw 'C','0','0','0','0',' ','O','0','0','0','0','0','0','0','0',13,10,0

    times 0x110 - ($ - $$) db 0
success_b14:
    ; NG_S0_T04_OK\r\n — succès temporaire du tracer B1.4.
    dw 'N','G','_','S','0','_','T','0','4','_','O','K',13,10,0
abi_kw:
    db 'abi'
efi_x64_kw:
    db 'efi_x64'

    times 0x140 - ($ - $$) db 0
diagnostic_e603:
    dw 'N','G','S','0',' ','E','6','0','3',' ','L','0','0','0','0',' '
    dw 'C','0','0','0','0',' ','O','0','0','0','0','0','0','0','0',13,10,0

    times 0x190 - ($ - $$) db 0
diagnostic_e604:
    dw 'N','G','S','0',' ','E','6','0','4',' ','L','0','0','0','0',' '
    dw 'C','0','0','0','0',' ','O','0','0','0','0','0','0','0','0',13,10,0

    times 0x1E0 - ($ - $$) db 0
diagnostic_e500:
    dw 'N','G','S','0',' ','E','5','0','0',' ','L','0','0','0','0',' '
    dw 'C','0','0','0','0',' ','O','0','0','0','0','0','0','0','0',13,10,0

    times 0x230 - ($ - $$) db 0
diagnostic_e001:
    dw 'N','G','S','0',' ','E','0','0','1',' ','L','0','0','0','0',' '
    dw 'C','0','0','0','0',' ','O','0','0','0','0','0','0','0','0',13,10,0

    times 0x280 - ($ - $$) db 0
diagnostic_e002:
    dw 'N','G','S','0',' ','E','0','0','2',' ','L','0','0','0','0',' '
    dw 'C','0','0','0','0',' ','O','0','0','0','0','0','0','0','0',13,10,0

bootstrap_external_asm_tag:
    db 'BOOTSTRAP_EXTERNAL_ASM',0

    times 0x300 - ($ - $$) db 0
reloc_anchor:
    dq IMAGE_BASE + entry
rdata_end:
    times 0x400 - ($ - $$) db 0

SECTION .data start=0x1A00 vstart=0x4000 align=8
data_start:
    dq 0
data_end:
    times 0x200 - ($ - $$) db 0

SECTION .reloc start=0x1C00 vstart=0x5000 align=4
reloc_start:
    dd 0x3000
    dd 12
    dw 0xA000 | (reloc_anchor - rdata_start)
    dw 0
reloc_end:
    times 0x200 - ($ - $$) db 0
