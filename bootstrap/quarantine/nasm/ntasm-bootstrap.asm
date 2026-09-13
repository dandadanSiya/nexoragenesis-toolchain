; BOOTSTRAP_EXTERNAL_ASM
; Source actif unique du bootstrap NTASM : chantier global vers B2.1.
; Éditer ce fichier en place; les seed-b1.*.asm antérieurs restent historiques.
; Commande de matérialisation quarantainée :
; nasm -f bin bootstrap/quarantine/nasm/ntasm-bootstrap.asm -o bootstrap/quarantine/nasm/out/ntasm-bootstrap.efi

; La commande normale produit désormais un assembleur, pas le tracer d'analyse.
; VALIDATE_ONLY est réservé aux anciens oracles de test, jamais à une release.
%ifndef VALIDATE_ONLY
    %define PRODUCE_PE 1
%endif

%ifdef PRODUCE_PE
; Les deux passes comparent les records exacts sans doubler les grandes tables.
%macro SAVE_PASS_RECORD 2
    push rax
    push rdx
    push r11
    mov rdx, %1
    mov rax, [rbp + F_WORK]
    cmp qword [rax + 0x100], 2
    jne %%done
    add rax, 0x1CC00
    %assign n 0
    %rep %2 / 8
        mov r11, [rdx + n]
        mov [rax + n], r11
        %assign n n+8
    %endrep
%%done:
    pop r11
    pop rdx
    pop rax
%endmacro
%macro CHECK_PASS_RECORD 3
    push rax
    push rdx
    push r8
    push r11
    mov rdx, %1
    mov rax, [rbp + F_WORK]
    cmp qword [rax + 0x100], 2
    jne %%ok
    add rax, 0x1CC00
    xor r8d, r8d
    %assign n 0
    %rep %2
        mov r11b, [rdx + n]
        xor r11b, [rax + n]
        %if n = 46
            and r11b, 0x7F       ; Début instruction rétabli par les instructions de pass 2.
        %endif
        or r8b, r11b
        %assign n n+1
    %endrep
    test r8b, r8b
    jmp %%restore
%%ok:
    xor r8d, r8d
%%restore:
    pop r11
    pop r8
    pop rdx
    pop rax
    jnz %3
%endmacro
%endif

BITS 64
ORG 0

%ifdef B18_SNAPSHOT               ; Instrumentation réservée au contrôle mémoire C01.
    %ifdef B15_SNAPSHOT_MULTI
        %error "B18_SNAPSHOT exclut le probe B15"
    %endif
    %ifdef B16_SNAPSHOT_CASE
        %error "B18_SNAPSHOT exclut le probe B16"
    %endif
    %ifdef B17_SNAPSHOT_CASE
        %error "B18_SNAPSHOT exclut le probe B17"
    %endif
%endif

%ifdef B16_SNAPSHOT_CASE
    %if (B16_SNAPSHOT_CASE != 1) && (B16_SNAPSHOT_CASE != 2) && (B16_SNAPSHOT_CASE != 7) && (B16_SNAPSHOT_CASE != 8)
        %error "B16_SNAPSHOT_CASE doit valoir 1, 2, 7 ou 8"
    %endif
    %ifdef B15_SNAPSHOT_MULTI
        %error "Les probes B15 et B16 sont mutuellement exclusifs"
    %endif
%endif

%ifdef B17_SNAPSHOT_CASE
    %if (B17_SNAPSHOT_CASE < 0) || (B17_SNAPSHOT_CASE > 5)
        %error "B17_SNAPSHOT_CASE doit valoir de 0 a 5"
    %endif
    %ifdef B15_SNAPSHOT_MULTI
        %error "Les probes B15 et B17 sont mutuellement exclusifs"
    %endif
    %ifdef B16_SNAPSHOT_CASE
        %error "Les probes B16 et B17 sont mutuellement exclusifs"
    %endif
%endif

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
%define F_CONST_NAME_PACK       0xE8
%define F_TOKEN_START           0xF0
%define F_TOKEN_END             0xF8
%define F_EXTRA_START           0x100
%define F_AFTER_START           0x108
%define F_CONST_META            0x110
%define FRAME_SIZE              0x118

; Après Close(source), ces slots UEFI morts deviennent l'état sémantique B1.3.
%define F_SEM_INDEX             F_INFO_SIZE
%define F_SEM_STAGE             F_READ_SIZE
%define F_SEM_BEGIN             F_LOADED
%define F_SEM_START             F_REMAINING
%define F_SEM_LINE              F_READ_STATUS
%define F_SYMBOL_COUNT          F_SIMPLEFS

%define EXPECT_FORMAT           0
%define EXPECT_ABI              1
%define CONST_OR_ENTRY          2
%define AFTER_ENTRY             3        ; Après entry, attendre la section .text.
%define IN_DATA                 6        ; Dernière section, lecture et écriture.
%define IN_RDATA                5        ; Section de données en lecture seule ouverte.
%define IN_TEXT                 4        ; L'en-tête .text rx a été validé.
%define F_TEXT_OFFSET           F_CONST_META ; Ce temporaire mort devient l'offset texte.

; Après validation complète d'entry, les temporaires de constante sont morts.
; Les cinq qwords contigus forment alors le descripteur d'entrée autoritatif.
%define F_ENTRY_SEEN            F_CONST_NAME_PACK
%define F_ENTRY_NAME_OFF        F_TOKEN_START
%define F_ENTRY_NAME_LEN        F_TOKEN_END
%define F_ENTRY_LINE            F_EXTRA_START
%define F_ENTRY_LINE_START      F_AFTER_START

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

; Layout PE centralisé : chaque taille brute est définie une seule fois.
; Les adresses fichier sont dérivées pour empêcher un décalage de section oublié.
%define PE_HEADER_RAW           0x400
%define PE_TEXT_RAW             0x6000
%define PE_RDATA_RAW            0x800
%define PE_DATA_RAW             0x200
%define PE_RELOC_RAW            0x200
%define PE_TEXT_FILE            PE_HEADER_RAW
%define PE_RDATA_FILE           (PE_TEXT_FILE + PE_TEXT_RAW)
%define PE_DATA_FILE            (PE_RDATA_FILE + PE_RDATA_RAW)
%define PE_RELOC_FILE           (PE_DATA_FILE + PE_DATA_RAW)
%define PE_FILE_BYTES           (PE_RELOC_FILE + PE_RELOC_RAW)
%define PE_PAGE_ALIGN           0x1000
%define PE_TEXT_RVA             PE_PAGE_ALIGN
%define PE_RDATA_RVA            (PE_TEXT_RVA + ((PE_TEXT_RAW + PE_PAGE_ALIGN - 1) & -PE_PAGE_ALIGN))
%define PE_DATA_RVA             (PE_RDATA_RVA + ((PE_RDATA_RAW + PE_PAGE_ALIGN - 1) & -PE_PAGE_ALIGN))
%define PE_RELOC_RVA            (PE_DATA_RVA + ((PE_DATA_RAW + PE_PAGE_ALIGN - 1) & -PE_PAGE_ALIGN))
%define PE_IMAGE_BYTES          (PE_RELOC_RVA + ((PE_RELOC_RAW + PE_PAGE_ALIGN - 1) & -PE_PAGE_ALIGN))
%if PE_IMAGE_BYTES > 0x10000
    %error "L'image virtuelle du bootstrap depasse 64 Kio"
%endif
%if PE_FILE_BYTES > 0x10000
    %error "Le bootstrap PE depasse le budget de 64 Kio"
%endif

SECTION .header start=0x0000 vstart=0x0000 align=1

pe_header_template:
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
    dd PE_TEXT_RAW
    dd PE_RDATA_RAW + PE_DATA_RAW + PE_RELOC_RAW
    dd 0
    dd entry
    dd PE_TEXT_RVA
    dq IMAGE_BASE
    dd PE_PAGE_ALIGN
    dd 0x0200
    dw 0,0,0,0,0,0
    dd 0
    dd PE_IMAGE_BYTES
    dd PE_HEADER_RAW
    dd 0
    dw 10
    dw 0x0140
    dq 0x100000,0x1000,0x100000,0x1000
    dd 0
    dd 16

    ; 16 data directories; seule Base Relocation (index 5) est non nulle.
    times 5 dq 0
    dd PE_RELOC_RVA, 0x000C
    times 10 dq 0

    ; Table des quatre sections, offset 0x188.
    db '.text',0,0,0
    dd text_end - text_start
    dd PE_TEXT_RVA
    dd PE_TEXT_RAW
    dd PE_TEXT_FILE
    dd 0,0
    dw 0,0
    dd 0x60000020

    db '.rdata',0,0
    dd rdata_end - rdata_start
    dd PE_RDATA_RVA
    dd PE_RDATA_RAW
    dd PE_RDATA_FILE
    dd 0,0
    dw 0,0
    dd 0x40000040

    db '.data',0,0,0
    dd data_end - data_start
    dd PE_DATA_RVA
    dd PE_DATA_RAW
    dd PE_DATA_FILE
    dd 0,0
    dw 0,0
    dd 0xC0000040

    db '.reloc',0,0
    dd reloc_end - reloc_start
    dd PE_RELOC_RVA
    dd PE_RELOC_RAW
    dd PE_RELOC_FILE
    dd 0,0
    dw 0,0
    dd 0x42000040

    times PE_HEADER_RAW - ($ - $$) db 0

SECTION .text start=PE_TEXT_FILE vstart=PE_TEXT_RVA align=16
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
    mov ecx, 30                     ; zéro jusqu'aux slots temporaires B1.8
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
%ifdef PRODUCE_PE
    mov rax, [rbp + F_WORK]
    mov qword [rax + 0x168], -1 ; Préfixe valide de la ligne, ou -1 si aucun défaut physique.
%endif

    jmp scan_loop
%ifdef PRODUCE_PE
; EAX=code, RCX=colonne, R8=offset, R9=ligne. Préserver tous les registres du scan.
record_physical_error:
    push rdi
    push rax
    push rcx
    push rdx
    push r8
    push r9
    push r10
    push r11
    sub rsp, 0x28
    mov edi, eax
    mov rax, r9
    mov rdx, rcx
    mov rcx, r8
    call remember_semantic_error
    add rsp, 0x28
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rcx
    pop rax
    pop rdi
    ret
%endif

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
%ifdef PRODUCE_PE
    mov rax, [rbp + F_WORK]
    mov rax, [rax + 0x168]
    cmp rax, -1
    jne .prefix_length
%endif
    mov eax, r11d
%ifdef PRODUCE_PE
.prefix_length:
%endif
    shl rax, 32
    or rdx, rax
    movzx eax, cl
    shl rax, 48
    or rdx, rax
    mov [rsi], rdx
%ifdef PRODUCE_PE
    mov rax, [rbp + F_WORK]
    mov qword [rax + 0x168], -1
%endif
    inc dword [rbp + F_LINES_DONE]
    add r8, rcx
    mov r10, r8
    xor r11d, r11d
    inc r9d
    jmp scan_loop

scan_bad:
%ifdef PRODUCE_PE
    mov rax, [rbp + F_WORK]
    cmp qword [rax + 0x168], -1
    jne scan_skip_invalid
    mov [rax + 0x168], r11
    mov eax, 100
    lea rcx, [r11 + 1]
    call record_physical_error
    jmp scan_skip_invalid
%else
    mov edi, 100
    mov rax, r9
    lea rdx, [r11 + 1]
    mov rcx, r8
    jmp fail_positioned

%endif
scan_line_long:
%ifdef PRODUCE_PE
    mov rax, [rbp + F_WORK]
    cmp qword [rax + 0x168], -1
    jne scan_skip_invalid
    mov [rax + 0x168], r11
    mov eax, 101
    mov ecx, 256
    call record_physical_error
scan_skip_invalid:
    inc r11
    inc r8
    jmp scan_loop
%else
    mov edi, 101
    mov rax, r9
    mov edx, 0x100
    mov rcx, r8
    jmp fail_positioned

%endif
scan_line_over:
%ifdef PRODUCE_PE
    mov eax, 500
    mov ecx, 1
    call record_physical_error
    jmp scan_complete            ; Aucun record au-delà de la capacité physique.
%else
    mov edi, 500
    mov rax, r9
    mov edx, 1
    mov rcx, r8
    jmp fail_positioned

%endif
scan_eof:
    test r11, r11
    jnz scan_eof_bad

scan_complete:
    call verify_guards              ; checkpoint post-scan
    test eax, eax
    jnz fail_e603

%ifdef PRODUCE_PE
    mov rax, [rbp + F_WORK]
    mov qword [rax + 0x100], 1
%endif
    jmp semantic_restart
%ifdef PRODUCE_PE
; Snapshot borné avant une ligne; seules les lignes valides publient leur état.
begin_semantic_transaction:
    push rax
    push rcx
    mov rax, [rbp + F_WORK]
    mov qword [rax + 0x108], 1
    mov rcx, [rbp + F_SEM_STAGE]
    mov [rax + 0x300], rcx
    mov rcx, [rbp + F_SYMBOL_COUNT]
    mov [rax + 0x308], rcx
    mov rcx, [rbp + F_TEXT_OFFSET]
    mov [rax + 0x310], rcx
    mov rcx, [rbp + F_ENTRY_SEEN]
    mov [rax + 0x340], rcx
    mov rcx, [rbp + F_ENTRY_NAME_OFF]
    mov [rax + 0x348], rcx
    mov rcx, [rbp + F_ENTRY_NAME_LEN]
    mov [rax + 0x350], rcx
    mov rcx, [rbp + F_ENTRY_LINE]
    mov [rax + 0x358], rcx
    mov rcx, [rbp + F_ENTRY_LINE_START]
    mov [rax + 0x360], rcx
    mov rcx, [rax + 0x8]
    mov [rax + 0x318], rcx
    mov rcx, [rax + 0x10]
    mov [rax + 0x320], rcx
    mov rcx, [rax + 0x30]
    mov [rax + 0x328], rcx
    mov rcx, [rax + 0x40]
    mov [rax + 0x330], rcx
    mov rcx, [rax + 0x60]
    mov [rax + 0x338], rcx
    pop rcx
    pop rax
    ret

rollback_semantic_transaction:
    mov rax, [rbp + F_WORK]
    mov rcx, [rax + 0x300]
    mov [rbp + F_SEM_STAGE], rcx
    mov rcx, [rax + 0x308]
    mov [rbp + F_SYMBOL_COUNT], rcx
    mov rcx, [rax + 0x310]
    mov [rbp + F_TEXT_OFFSET], rcx
    mov rcx, [rax + 0x340]
    mov [rbp + F_ENTRY_SEEN], rcx
    mov rcx, [rax + 0x348]
    mov [rbp + F_ENTRY_NAME_OFF], rcx
    mov rcx, [rax + 0x350]
    mov [rbp + F_ENTRY_NAME_LEN], rcx
    mov rcx, [rax + 0x358]
    mov [rbp + F_ENTRY_LINE], rcx
    mov rcx, [rax + 0x360]
    mov [rbp + F_ENTRY_LINE_START], rcx
    mov rcx, [rax + 0x318]
    mov [rax + 0x8], rcx
    mov rcx, [rax + 0x320]
    mov [rax + 0x10], rcx
    mov rcx, [rax + 0x328]
    mov [rax + 0x30], rcx
    mov rcx, [rax + 0x330]
    mov [rax + 0x40], rcx
    mov rcx, [rax + 0x338]
    mov [rax + 0x60], rcx
    mov rcx, [rbp + F_SYMBOL_COUNT]
    mov [rax], ecx
    mov qword [rax + 0x20], 0
    mov qword [rax + 0x28], 0
    mov qword [rax + 0x108], 0
    ; Une instruction invalide ne doit pas fabriquer un second défaut d'entrée.
    ; Le bit 8 est uniquement un état de récupération; aucun fichier ne sera produit.
    cmp qword [rbp + F_SEM_STAGE], IN_TEXT
    jne .done
    mov rdx, [rbp + F_TEXT_OFFSET]
    mov rcx, [rbp + F_SYMBOL_COUNT]
    lea r10, [rax + 0x400]
.mark_invalid_site:
    test rcx, rcx
    jz .done
    movzx r11d, word [r10 + 46]
    and r11d, 0x6F
    cmp r11d, 0x22
    jne .next
    cmp [r10 + 32], rdx
    jne .next
    or word [r10 + 46], 0x100
.next:
    add r10, 48
    dec rcx
    jmp .mark_invalid_site
.done:
    ret

; EDI=code, RAX=ligne, RDX=colonne, RCX=offset; garder le plus petit offset.
remember_semantic_error:
    mov r10, [rbp + F_WORK]
    cmp qword [r10 + 0x110], 0
    je .store
    cmp rcx, [r10 + 0x128]
    jae .done
.store:
    mov [r10 + 0x110], rdi
    mov [r10 + 0x118], rax
    mov [r10 + 0x120], rdx
    mov [r10 + 0x128], rcx
.done:
    ret
load_semantic_error:
    mov r10, [rbp + F_WORK]
    mov rdi, [r10 + 0x110]
    mov rax, [r10 + 0x118]
    mov rdx, [r10 + 0x120]
    mov rcx, [r10 + 0x128]
    ret
%endif

semantic_restart:
    mov qword [rbp + F_SEM_INDEX], 0
    mov qword [rbp + F_SEM_STAGE], 0
    mov qword [rbp + F_SYMBOL_COUNT], 0
    mov rax, [rbp + F_WORK]
    mov dword [rax], 0

semantic_next_record:
%ifdef PRODUCE_PE
    call finish_semantic_line
    test eax, eax
    jz .line_ok
    mov edi, eax
    mov rax, [rbp + F_SEM_LINE]
    mov edx, 1
    mov rcx, [rbp + F_SEM_START]
    jmp fail_positioned
.line_ok:
%endif
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
%ifdef PRODUCE_PE
    call begin_semantic_transaction
%endif
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
    xor r10d, r10d                ; État de guillemets pour trouver les vrais commentaires.
.comment_loop:
    cmp rsi, r11
    jae .trim_trailing
    mov al, [rdx + rsi]
    cmp al, 0x22                  ; Les guillemets échappés sont sautés plus bas.
    je .quote_toggle
    test r10d, r10d
    jz .outside_quote
    cmp al, 0x5C
    jne .comment_advance
    inc rsi                       ; Sauter le caractère échappé sans le valider ici.
    cmp rsi, r11
    jae .trim_trailing
    jmp .comment_advance
.outside_quote:
    cmp al, ';'
    je .comment_found
    jmp .comment_advance
.quote_toggle:
    xor r10d, 1
.comment_advance:
    inc rsi
    jmp .comment_loop
.comment_found:
    mov r9, rsi

.trim_trailing:
    test r10d, r10d                ; Garder la vraie fin d'une chaîne non fermée.
    jnz .semantic_line
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
    mov r9, [rbp + F_SEM_STAGE]
    cmp r9d, IN_TEXT               ; Les labels n'existent que dans une section ouverte.
    jb .dispatch_statement
    call b111_try_label            ; EAX : 0=autre, 1=inséré, sinon code d'erreur.
    test eax, eax
    jz .refresh_stage
    cmp eax, 1
    je semantic_next_record
    cmp eax, 603                   ; L'état interne n'a pas de position source.
    je fail_e603
    xchg rax, rdx                 ; Convertir code/colonne en protocole hérité.
    jmp semantic_const_error
.refresh_stage:
    call b112_try_align            ; La mesure d'alignement suit la reconnaissance de label.
    test eax, eax
    jz .refresh_after_align
    cmp eax, 1
    je semantic_next_record
    cmp eax, 603
    je fail_e603
    xchg rax, rdx
    jmp semantic_const_error
.refresh_after_align:
    call try_scalar_data           ; Famille byte/word/dword/qword, mêmes règles et encodeur.
    test eax, eax
    jz .refresh_after_scalar
    cmp eax, 1
    je semantic_next_record
    cmp eax, 603
    je fail_e603
    xchg rax, rdx
    jmp semantic_const_error
.refresh_after_scalar:
    call try_text_data
    test eax, eax
    jz .refresh_after_text_data
    cmp eax, 1
    je semantic_next_record
    cmp eax, 603
    je fail_e603
    xchg rax, rdx
    jmp semantic_const_error
.refresh_after_text_data:
    call try_branch_instruction
    test eax, eax
    jz .refresh_after_branch
    cmp eax, 1
    je semantic_next_record
    cmp eax, 603
    je fail_e603
    xchg rax, rdx
    jmp semantic_const_error
.refresh_after_branch:
    call try_register_instruction
    test eax, eax
    jz .refresh_after_instruction
    cmp eax, 1
    je semantic_next_record
    cmp eax, 603
    je fail_e603
    xchg rax, rdx
    jmp semantic_const_error
.refresh_after_instruction:
    call try_memory_instruction
    test eax, eax
    jz .refresh_after_memory
    cmp eax, 1
    je semantic_next_record
    cmp eax, 603
    je fail_e603
    xchg rax, rdx
    jmp semantic_const_error
.refresh_after_memory:
    mov r9, [rbp + F_SEM_STAGE]    ; Le détecteur peut employer les registres volatils.
.dispatch_statement:
    cmp r9d, CONST_OR_ENTRY
    je semantic_const_or_entry
    cmp r9d, AFTER_ENTRY
    je semantic_after_entry
    cmp r9d, IN_TEXT                ; Le cinquième état classe le corps texte.
    je semantic_in_text             ; Aucun statement du corps n'est encore accepté.
    cmp r9d, IN_RDATA                ; Nouvel état de la section en lecture seule.
    je semantic_in_rdata
    cmp r9d, IN_DATA                ; Le corps final interdit toute nouvelle section.
    je semantic_in_data
    call match_preamble_line
    test rax, rax
    jnz semantic_e200
    inc qword [rbp + F_SEM_STAGE]
    jmp semantic_next_record

semantic_const_or_entry:
    call classify_const_or_entry
    cmp eax, 1
    je .const
    cmp eax, 2
    je .entry
    jmp semantic_e200_at_begin
.const:
    call match_const_prefix
    test rax, rax
    jnz semantic_const_error
    test edx, edx                   ; 1=const insérée, 0=suffixe non-const
    jz fail_e603
    jmp semantic_next_record
.entry:
    call match_entry_line
    test rax, rax
    jnz semantic_const_error
    cmp edx, 1
    jne fail_e603
    mov qword [rbp + F_SEM_STAGE], AFTER_ENTRY
    jmp semantic_next_record

semantic_after_entry:              ; Analyser exactement section WS+ .text WS+ rx.
    mov rdi, r8                    ; Garder la fin logique pour une erreur de token absent.
    call b18_lex_header            ; Remplir les tranches de tokens dans WORK.
    test rax, rax                  ; Un index + 1 non nul signale une limite lexicale.
    jnz semantic_const_error       ; Convertir la position locale en diagnostic global.
    mov ebx, edx                   ; Mémoriser le nombre de tokens validés.
    mov rsi, [rbp + F_WORK]         ; WORK reste possédé jusqu'au cleanup.
    add rsi, 0x1C400               ; Table réservée : 16 records de 16 octets.
    mov rdx, rsi                   ; Premier record : début et longueur du token.
    lea r8, [rel b18_section_kw]    ; Exiger le mot-clé section en minuscules.
    mov r9d, 7                     ; Longueur exacte de section.
    call b18_token_equal           ; Comparaison de tranche, sans chaîne terminée par zéro.
    test eax, eax                  ; EAX vaut 1 seulement en cas d'égalité.
    jz semantic_e200_at_begin       ; Premier token incorrect : E200 à son début.
    cmp ebx, 2                     ; Le nom de section doit être présent.
    jb .missing                    ; Positionner E201 à la fin logique sinon.
    mov rax, [rsi]                 ; Début du premier token.
    add rax, [rsi + 8]            ; Calculer sa fin exclusive.
    cmp [rsi + 16], rax           ; Le prochain token doit être séparé par WS.
    jbe .gap_second               ; Un point adjacent révèle le séparateur absent.
    lea rdx, [rsi + 16]           ; Second record : nom de section.
    lea r8, [rel b18_text_kw]      ; Nom .text exact, sans accepter .textrx.
    mov r9d, 5                    ; Le point est inclus dans ces cinq octets.
    cmp qword [rbp + F_SEM_STAGE], IN_TEXT ; Après .text, exiger .rdata.
    je .name_rdata
    cmp qword [rbp + F_SEM_STAGE], IN_RDATA
    jne .name_ready
    lea r8, [rel b110_data_kw]     ; Après .rdata, exiger .data, longueur cinq.
    jmp .name_ready
.name_rdata:
    lea r8, [rel b19_rdata_kw]     ; La deuxième section porte six caractères.
    mov r9d, 6
.name_ready:
    call b18_token_equal           ; Ne lit aucun octet au-delà de la tranche.
    test eax, eax                 ; Vérifier le résultat de la comparaison.
    jz .bad_second                ; Une mauvaise section produit E200 au second token.
    cmp ebx, 3                    ; Les droits rx sont obligatoires.
    jb .missing                   ; Leur absence est une erreur de syntaxe.
    mov rax, [rsi + 16]           ; Début de .text.
    add rax, [rsi + 24]           ; Fin exclusive de .text.
    cmp [rsi + 32], rax           ; Exiger un espace ou une tabulation avant rx.
    jbe .gap_third                ; Une virgule adjacente produit E201.
    lea rdx, [rsi + 32]           ; Troisième record : droits de la section.
    lea r8, [rel b18_rx_kw]        ; Seuls les droits rx sont reconnus.
    mov r9d, 2                    ; Deux octets exacts, sensibles à la casse.
    cmp qword [rbp + F_SEM_STAGE], IN_TEXT
    je .rights_rdata
    cmp qword [rbp + F_SEM_STAGE], IN_RDATA
    jne .rights_ready
    lea r8, [rel b110_rw_kw]       ; La dernière section accepte uniquement rw.
    jmp .rights_ready
.rights_rdata:
    lea r8, [rel b19_r_kw]         ; .rdata accepte la lecture seule.
    mov r9d, 1
.rights_ready:
    call b18_token_equal           ; Comparer les droits après validation des séparateurs.
    test eax, eax                 ; Une valeur différente est interdite.
    jz .bad_third                 ; E200 vise le début des droits incorrects.
    cmp ebx, 3                    ; Aucun token supplémentaire n'est admis.
    ja .extra                     ; E201 vise le quatrième token.
    cmp qword [rbp + F_SEM_STAGE], IN_TEXT
    je .enter_rdata
    cmp qword [rbp + F_SEM_STAGE], IN_RDATA
    je .enter_data
    mov qword [rbp + F_TEXT_OFFSET], 0 ; Aucun encodeur ne mesure encore du code.
%ifdef B112_INITIAL_TEXT_OFFSET
    mov rax, B112_INITIAL_TEXT_OFFSET ; Simulation de préfixe, absente du canonique.
    mov [rbp + F_TEXT_OFFSET], rax
%endif
    mov qword [rbp + F_SEM_STAGE], IN_TEXT ; Fermer définitivement l'en-tête texte.
    jmp semantic_next_record      ; Continuer à détecter les statements interdits.
.enter_rdata:
    mov rax, [rbp + F_WORK]        ; État réservé au début du pool de travail.
    mov qword [rax + 8], 0         ; Offset rdata nul tant que les directives sont absentes.
    mov qword [rbp + F_SEM_STAGE], IN_RDATA
    jmp semantic_next_record      ; Examiner toutes les lignes suivantes.
.enter_data:
    mov rax, [rbp + F_WORK]        ; WORK conserve les compteurs des payloads.
    mov qword [rax + 16], 0        ; Offset data nul avant ses encodeurs.
    mov qword [rbp + F_SEM_STAGE], IN_DATA
    jmp semantic_next_record      ; Vérifier tout ce qui suit, jusqu'à EOF.
.missing:                         ; Token obligatoire absent.
    lea rax, [rdi + 1]            ; Fin logique zéro-based vers colonne un-based.
    jmp .syntax                   ; L'absence relève de E201.
.gap_second:                      ; Le token .text est collé à section.
    mov rax, [rsi + 16]           ; Viser le premier octet du second token.
    jmp .syntax_index             ; Convertir l'index en colonne.
.gap_third:                       ; Les droits ou une ponctuation sont collés à .text.
    mov rax, [rsi + 32]           ; Viser le premier octet du troisième token.
    jmp .syntax_index             ; Convertir l'index en colonne.
.extra:                           ; Le lexer a permis au maximum 16 tokens.
    mov rax, [rsi + 48]           ; Début du quatrième token excédentaire.
.syntax_index:                    ; Les records stockent des index zéro-based.
    inc rax                       ; Le protocole d'erreur renvoie index + 1.
.syntax:                          ; Erreur de séparateur ou de nombre de tokens.
    mov edx, 201                  ; Sélectionner le diagnostic de syntaxe.
    jmp semantic_const_error      ; Réutiliser le formateur de position hérité.
.bad_second:                      ; Nom de section incorrect.
    cmp qword [rbp + F_SEM_STAGE], IN_TEXT
    je .duplicate_text
    cmp qword [rbp + F_SEM_STAGE], IN_RDATA
    jne .bad_second_position
    lea r8, [rel b19_rdata_kw]     ; Une deuxième .rdata conserve E200 au mot section.
    mov r9d, 6
    jmp .duplicate_compare
.duplicate_text:
    lea r8, [rel b18_text_kw]      ; Préserver le diagnostic historique de .text dupliquée.
    mov r9d, 5
.duplicate_compare:
    call b18_token_equal
    test eax, eax
    jnz semantic_e200_at_begin
.bad_second_position:
    mov rax, [rsi + 16]           ; Pointer la section responsable.
    jmp .order                    ; Produire E200.
.bad_third:                       ; Droits de section incorrects.
    mov rax, [rsi + 32]           ; Pointer les droits responsables.
.order:                           ; Les valeurs et l'ordre d'en-tête relèvent de E200.
    inc rax                       ; Convertir en colonne physique un-based.
    mov edx, 200                  ; Sélectionner le diagnostic d'ordre.
    jmp semantic_const_error      ; Conserver ligne et offset source exacts.

semantic_in_text:                 ; Détecter la transition avant le classement du corps.
    call b19_is_section            ; Ce test conserve les arguments de ligne.
    test eax, eax
    jnz semantic_after_entry       ; Réutiliser le parseur de section borné.
    lea r9, [rel b18_body_words]    ; Données interdites dans le texte : E202.
    call b18_body_error           ; Renvoie E200, E202 ou E400 avec index + 1.
    jmp semantic_const_error      ; Ne jamais accepter un statement inconnu en silence.

semantic_in_rdata:
    call b19_is_section            ; La transition vers .data reste possible ici seulement.
    test eax, eax
    jnz semantic_after_entry
semantic_in_data:                 ; Aucun nouvel en-tête n'est permis après .data.
    lea r9, [rel b19_data_words]   ; Instructions interdites dans les données : E202.
    call b18_body_error
    jmp semantic_const_error

%ifdef PRODUCE_PE
; A chaque frontière de ligne : mesurer exactement, puis émettre seulement en pass 2.
finish_semantic_line:
    push rbx
    push rsi
    push rdi
    sub rsp, 0x20
    mov rbx, [rbp + F_WORK]
    mov rax, [rbp + F_SEM_INDEX]
    test rax, rax
    jz .reset
    xor r8d, r8d
    cmp qword [rbp + F_SEM_STAGE], IN_TEXT
    jb .sum_ready
    mov r8, [rbp + F_TEXT_OFFSET]
.sum_ready:
    add r8, [rbx + 8]
    add r8, [rbx + 16]
    mov r9, r8
    sub r9, [rbx + 0x70]
    jc .bad
    cmp r9, 0xFFFF
    ja .bad
    lea rdi, [rbx + rax*8 + 0x143F8]
    cmp qword [rbx + 0x100], 2
    je .compare
    mov [rdi + 6], r9w         ; Après scan, l'EOL est dans SRC; ce mot garde la taille exacte.
    jmp .remember_sum
.compare:
    cmp [rdi + 6], r9w
    jne .bad
    cmp [rbx + 0x20], r9
    jne .bad
    test r9, r9
    jz .remember_sum
    mov rax, [rbp + F_SEM_STAGE]
    sub rax, IN_TEXT
    cmp rax, 2
    ja .bad
    mov rdx, rax
    shl rdx, 5
    mov rdx, [rbx + rdx + 0x218] ; Pointeur raw de la section finale.
    test eax, eax
    jz .text_offset
    mov rcx, [rbx + rax*8]
    jmp .offset
.text_offset:
    mov rcx, [rbp + F_TEXT_OFFSET]
.offset:
    sub rcx, r9
    jc .bad
    add rdx, rcx
    mov rcx, rdx
    add rcx, r9
    cmp rcx, [rbx + 0x288]
    ja .bad
    mov rdi, [rbp + F_OUT]
    add rdi, rdx
    mov rcx, [rbx + 0x28]
    cmp ecx, 1
    je .bytes
    xor eax, eax
    cmp ecx, 2
    je .fill
    cmp ecx, 3
    jne .bad
    mov al, 0x90
.fill:
    mov [rdi], al
    inc rdi
    dec r9
    jnz .fill
    jmp .remember_sum
.bytes:
    cmp r9, 0x400
    ja .bad
    lea rsi, [rbx + 0x1C500]
.copy:
    mov al, [rsi]
    mov [rdi], al
    inc rsi
    inc rdi
    dec r9
    jnz .copy
.remember_sum:
    mov [rbx + 0x70], r8
.reset:
    mov qword [rbx + 0x20], 0
    mov qword [rbx + 0x28], 0
    xor eax, eax
    jmp .out
.bad:
    mov eax, 600
.out:
    add rsp, 0x20
    pop rdi
    pop rsi
    pop rbx
    ret

; Résolution différée de l'entrée avant les références situées plus loin.
; Retour commun EAX=code, R8=ligne, RDX=colonne, RCX=offset; zéro en succès.
validate_output_entry:
    sub rsp, 0x28
    mov rcx, [rbp + F_SRC]
    add rcx, [rbp + F_ENTRY_NAME_OFF]
    mov rdx, [rbp + F_ENTRY_NAME_LEN]
    call lookup_symbol_name
    test rax, rax
    jz .bad
    cmp word [rax + 46], 0xA2
    je .entry_rva
    movzx ecx, word [rax + 46]
    mov edx, ecx
    and ecx, 0x6F
    cmp ecx, 0x22
    jne .bad
    test edx, 0x100
    jz .bad
    mov r10, [rbp + F_WORK]
    cmp qword [r10 + 0x110], 0
    je .bad
.entry_rva:
    mov rax, [rax + 32]
    add rax, 0x1000
    mov r10, [rbp + F_WORK]
    cmp qword [r10 + 0x100], 2
    je .compare
    mov [r10 + 0x280], rax
    jmp .ok
.compare:
    cmp [r10 + 0x280], rax
    jne .mismatch
.ok:
    xor eax, eax
    add rsp, 0x28
    ret
.bad:
    mov eax, 302
    mov r8, [rbp + F_ENTRY_LINE]
    mov rcx, [rbp + F_ENTRY_NAME_OFF]
    mov rdx, rcx
    sub rdx, [rbp + F_ENTRY_LINE_START]
    inc rdx
    add rsp, 0x28
    ret
.mismatch:
    mov eax, 600
    xor ecx, ecx
    xor edx, edx
    xor r8d, r8d
    add rsp, 0x28
    ret

producer_pass_complete:
    mov rbx, [rbp + F_WORK]
    cmp qword [rbx + 0x100], 2
    je .second_pass
    cmp qword [rbp + F_TEXT_OFFSET], 0
    je fail_e200_eof
    cmp qword [rbx + 8], 0
    je fail_e200_eof
    cmp qword [rbx + 16], 0
    je fail_e200_eof
    cmp qword [rbx + 0x60], 0
    je .no_reloc
    mov rax, [rbp + F_SYMBOL_COUNT]
    mov [rbx + 0x180], rax
    mov rax, [rbx + 0x30]
    mov [rbx + 0x188], rax
    mov rax, [rbx + 0x40]
    mov [rbx + 0x190], rax
    mov rax, [rbx + 0x60]
    mov [rbx + 0x198], rax
    call layout_output_pe
    test eax, eax
    jnz producer_internal_error
    ; Refaire toute l'analyse; les tables sont comparées record par record avant remplacement.
    xor ecx, ecx
.clear_state:
    mov qword [rbx + rcx], 0
    add rcx, 8
    cmp rcx, 0x100
    jb .clear_state
    mov qword [rbx + 0x100], 2
    xor ecx, ecx
    mov rdi, [rbp + F_OUT]
.clear_output:
    mov qword [rdi + rcx], 0
    add rcx, 8
    cmp rcx, 0x10000
    jb .clear_output
    mov qword [rbp + F_ENTRY_SEEN], 0
    mov qword [rbp + F_ENTRY_NAME_OFF], 0
    mov qword [rbp + F_ENTRY_NAME_LEN], 0
    mov qword [rbp + F_ENTRY_LINE], 0
    mov qword [rbp + F_ENTRY_LINE_START], 0
    mov qword [rbp + F_TEXT_OFFSET], 0
%ifdef TEST_PASS_SIZE_MISMATCH
    xor word [rbx + 0x14406], 1 ; Faute de test : différence de mesure, sans changer la source.
%endif
    jmp semantic_restart
.second_pass:
    mov rax, [rbp + F_SYMBOL_COUNT]
    cmp rax, [rbx + 0x180]
    jne .mismatch
    mov rax, [rbx + 0x30]
    cmp rax, [rbx + 0x188]
    jne .mismatch
    mov rax, [rbx + 0x40]
    cmp rax, [rbx + 0x190]
    jne .mismatch
    mov rax, [rbx + 0x60]
    cmp rax, [rbx + 0x198]
    jne .mismatch
    mov rax, [rbp + F_TEXT_OFFSET]
    cmp rax, [rbx + 0x200]
    jne .mismatch
    mov rax, [rbx + 8]
    cmp rax, [rbx + 0x220]
    jne .mismatch
    mov rax, [rbx + 16]
    cmp rax, [rbx + 0x240]
    jne .mismatch
    call finish_output_pe
    test eax, eax
    jnz producer_internal_error
    call verify_guards
    test eax, eax
    jnz fail_e603
%ifdef PRODUCER_HELLO_ASSERT
    mov rdi, [rbp + F_OUT]
    cmp word [rdi], 0x5A4D
    jne fail_e603
    cmp dword [rdi + 0x80], 0x4550
    jne fail_e603
    cmp byte [rdi + 0x400], 0x53
    jne fail_e603
    mov rax, 0x140001000
    cmp [rdi + 0x600], rax
    jne fail_e603
%endif
    call write_output_pe
    test eax, eax
    jz cleanup
    mov [rbp + F_DIAG], rax
    mov [rbp + F_EXIT_STATUS], rdx
    SET_ZERO_POSITION
    jmp cleanup
.no_reloc:
    mov eax, 602
    jmp producer_internal_error
.mismatch:
    mov eax, 600
producer_internal_error:
    mov [rbp + F_DIAG], rax
    mov rax, EFI_LOAD_ERROR
    mov [rbp + F_EXIT_STATUS], rax
    SET_ZERO_POSITION
    jmp cleanup

; Quatre descripteurs {virtual_size,RVA,raw_size,raw_offset}, qwords.
layout_output_pe:
    push rbx
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    sub rsp, 0x20
    mov rbx, [rbp + F_WORK]
    mov rax, [rbp + F_TEXT_OFFSET]
    mov [rbx + 0x200], rax
    mov rax, [rbx + 8]
    mov [rbx + 0x220], rax
    mov rax, [rbx + 16]
    mov [rbx + 0x240], rax
    lea rdi, [rbx + 0x200]
    mov r8d, 0x1000
    mov r9d, 0x400
    mov ecx, 3
.section:
    mov rax, [rdi]
    add rax, 0x1FF
    and rax, -0x200
    mov [rdi + 8], r8
    mov [rdi + 16], rax
    mov [rdi + 24], r9
    add r9, rax
    add r8, rax
    add r8, 0xFFF
    and r8, -0x1000
    add rdi, 32
    dec ecx
    jnz .section
    mov [rdi + 8], r8
    mov [rdi + 24], r9
    ; Les références sont déjà ordonnées par section/site dans la grammaire fermée.
    lea rsi, [rbx + 0x12400]
    mov r12, [rbx + 0x60]
    mov r13, -1
    xor r14d, r14d
    mov r15, -1
.reloc:
    movzx eax, word [rsi + 14]
    dec eax
    cmp eax, 1
    jb .bad
    cmp eax, 2
    ja .bad
    shl rax, 5
    mov rdx, [rbx + rax + 0x208]
    mov ecx, [rsi + 4]
    test ecx, 7
    jnz .bad
    add rdx, rcx
    cmp r15, -1
    je .ordered
    cmp rdx, r15
    jbe .bad
.ordered:
    mov r15, rdx
    and rdx, -0x1000
    cmp rdx, r13
    je .entry
    add r14, 3
    and r14, -4
    add r14, 8
    mov r13, rdx
.entry:
    add r14, 2
    add rsi, 16
    dec r12
    jnz .reloc
    add r14, 3
    and r14, -4
    cmp r14, 0x1000
    ja .large
    mov [rbx + 0x260], r14
    add r14, 0x1FF
    and r14, -0x200
    mov [rbx + 0x270], r14
    mov rax, [rbx + 0x278]
    add rax, r14
    cmp rax, 0x10000
    ja .large
    mov [rbx + 0x288], rax
    mov rax, [rbx + 0x268]
    add rax, r14
    add rax, 0xFFF
    and rax, -0x1000
    cmp rax, 0x10000
    ja .large
    mov [rbx + 0x290], rax
    xor eax, eax
    jmp .out
.large:
    mov eax, 601
    jmp .out
.bad:
    mov eax, 600
.out:
    add rsp, 0x20
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    ret

; Matérialiser headers et patchs dans OUT seulement après validation des deux passes.
finish_output_pe:
    push rbx
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    sub rsp, 0x20
    mov rbx, [rbp + F_WORK]
    mov rdi, [rbp + F_OUT]
    lea rsi, [rel pe_header_template]
    xor ecx, ecx
.header:
    mov rax, [rsi + rcx]
    mov [rdi + rcx], rax
    add rcx, 8
    cmp rcx, 0x400
    jb .header
    mov eax, [rbx + 0x210]
    mov [rdi + 0x9C], eax
    mov rax, [rbx + 0x230]
    add rax, [rbx + 0x250]
    add rax, [rbx + 0x270]
    mov [rdi + 0xA0], eax
    mov eax, [rbx + 0x280]
    mov [rdi + 0xA8], eax
    ; Le chargeur peut modifier ImageBase dans les headers chargés du producteur.
    ; La sortie utilise toujours la base préférée normative, jamais celle d'OVMF.
    mov rax, IMAGE_BASE
    mov [rdi + 0xB0], rax
    mov eax, [rbx + 0x290]
    mov [rdi + 0xD0], eax
    mov eax, [rbx + 0x268]
    mov [rdi + 0x130], eax
    mov eax, [rbx + 0x260]
    mov [rdi + 0x134], eax
    lea rsi, [rbx + 0x200]
    lea rdx, [rdi + 0x188]
    mov ecx, 4
.sections:
    mov eax, [rsi]
    mov [rdx + 8], eax
    mov eax, [rsi + 8]
    mov [rdx + 12], eax
    mov eax, [rsi + 16]
    mov [rdx + 16], eax
    mov eax, [rsi + 24]
    mov [rdx + 20], eax
    add rsi, 32
    add rdx, 40
    dec ecx
    jnz .sections
    lea rsi, [rbx + 0x6400]
    mov r12, [rbx + 0x40]
.fixups:
    test r12, r12
    jz .addresses
    mov eax, [rsi + 4]
    lea rdx, [rax + 4]
    cmp rdx, [rbx + 0x200]
    ja .bad
    mov edx, [rsi + 20]
    mov [rdi + rax + 0x400], edx
    add rsi, 24
    dec r12
    jmp .fixups
.addresses:
    lea rsi, [rbx + 0x12400]
    mov r12, [rbx + 0x60]
    mov r13, -1               ; Page précédente, hors espace RVA.
    mov r14, [rbx + 0x278]    ; Début du bloc en cours.
    mov r15, r14              ; Curseur d'émission .reloc.
.reference:
    mov ecx, [rsi]
    add rcx, [rbp + F_SRC]
    movzx edx, word [rsi + 12]
    call lookup_symbol_name
    test rax, rax
    jz .bad
    movzx ecx, word [rax + 46]
    shr ecx, 5
    and ecx, 3
    dec ecx
    cmp ecx, 2
    ja .bad
    shl rcx, 5
    mov rdx, [rax + 32]
    add rdx, [rbx + rcx + 0x208]
    mov rax, IMAGE_BASE
    add rdx, rax
    movzx ecx, word [rsi + 14]
    dec ecx
    shl rcx, 5
    mov eax, [rsi + 4]
    lea r8, [rax + 8]
    cmp r8, [rbx + rcx + 0x200]
    ja .bad
    mov r8, [rbx + rcx + 0x218]
    add r8, rax
    mov [rdi + r8], rdx
    add rax, [rbx + rcx + 0x208]
    mov rdx, rax
    and rdx, -0x1000
    cmp rdx, r13
    je .reloc_entry
    cmp r13, -1
    je .new_block
    add r15, 3
    and r15, -4
    mov rcx, r15
    sub rcx, r14
    mov [rdi + r14 + 4], ecx
.new_block:
    mov r14, r15
    mov r13, rdx
    mov [rdi + r15], edx
    add r15, 8
.reloc_entry:
    and eax, 0xFFF
    or eax, 0xA000
    mov [rdi + r15], ax
    add r15, 2
    add rsi, 16
    dec r12
    jnz .reference
    add r15, 3
    and r15, -4
    mov rax, r15
    sub rax, r14
    mov [rdi + r14 + 4], eax
    sub r15, [rbx + 0x278]
    cmp r15, [rbx + 0x260]
    jne .bad
    xor eax, eax
    jmp .out
.bad:
    mov eax, 600
.out:
    add rsp, 0x20
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    ret

; Aucun écrasement : seule EFI_NOT_FOUND au probe READ autorise CREATE.
write_output_pe:
    push rbx
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    sub rsp, 0x30
    mov rbx, [rbp + F_ROOT]
    test rbx, rbx
    jz .create_error
    cmp qword [rbx + 8], 0
    je .create_error
    mov qword [rbp + F_SOURCE], 0
    mov rcx, rbx
    lea rdx, [rbp + F_SOURCE]
    lea r8, [rel output_filename]
    mov r9d, 1
    mov qword [rsp + 0x20], 0
    call [rbx + 8]
    test rax, rax
    js .probe_error
    mov rdi, [rbp + F_SOURCE]
    test rdi, rdi
    jz .create_error
    or qword [rbp + F_OWNERSHIP], OWN_SOURCE
    mov eax, 700
    mov rdx, 0x8000000000000007
    jmp .out
.probe_error:
    mov rdx, 0x800000000000000E
    cmp rax, rdx
    jne .create_status
    mov rcx, rbx
    lea rdx, [rbp + F_SOURCE]
    lea r8, [rel output_filename]
    mov r9, 0x8000000000000003
    mov qword [rsp + 0x20], 0
    call [rbx + 8]
    test rax, rax
    js .create_status
    mov rdi, [rbp + F_SOURCE]
    test rdi, rdi
    jz .create_error
    or qword [rbp + F_OWNERSHIP], OWN_SOURCE
    cmp qword [rdi + 0x28], 0
    je .write_error
    cmp qword [rdi + 0x50], 0
    je .write_error
    cmp qword [rdi + 0x10], 0
    je .write_error
    mov rax, [rbp + F_WORK]
    mov rax, [rax + 0x288]
    mov [rbp + F_INFO_SIZE], rax
%ifdef TEST_WRITE_SHORT
    dec qword [rbp + F_INFO_SIZE] ; Le retour sera comparé à la taille complète, pas à la requête tronquée.
%endif
    mov rcx, rdi
    lea rdx, [rbp + F_INFO_SIZE]
    mov r8, [rbp + F_OUT]
%ifdef PRODUCER_HELLO_ASSERT
    cmp word [r8], 0x5A4D
    jne .write_error
    cmp byte [r8 + 0x400], 0x53
    jne .write_error
%endif
    call [rdi + 0x28]
    test rax, rax
    js .write_status
    mov rax, [rbp + F_WORK]
    mov rax, [rax + 0x288]
    cmp [rbp + F_INFO_SIZE], rax
    jne .write_error
    mov rcx, rdi
    call [rdi + 0x50]
%ifdef TEST_FLUSH_FAILURE
    mov rax, 0x8000000000000007
%endif
    test rax, rax
    js .write_status
%ifdef PRODUCER_HELLO_ASSERT
    mov rax, [rbp + F_OUT]
    cmp word [rax], 0x5A4D
    jne .write_error
    cmp byte [rax + 0x400], 0x53
    jne .write_error
%endif
    mov rcx, rdi
    call [rdi + 0x10]
    mov qword [rbp + F_SOURCE], 0
    and qword [rbp + F_OWNERSHIP], ~OWN_SOURCE
%ifdef TEST_CLOSE_FAILURE
    mov rax, 0x8000000000000007
%endif
    test rax, rax
    js .write_status
    xor eax, eax
    xor edx, edx
    jmp .out
.create_status:
    mov rdx, rax
    mov eax, 701
    jmp .out
.create_error:
    mov eax, 701
    mov rdx, 0x8000000000000007
    jmp .out
.write_status:
    mov rdx, rax
    mov eax, 702
    jmp .out
.write_error:
    mov eax, 702
    mov rdx, 0x8000000000000007
.out:
    add rsp, 0x30
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    ret
%endif

semantic_success:
    mov rax, [rbp + F_WORK]
    cmp qword [rax + 16], 0x2000   ; Capacité maximale du payload data.
    ja fail_e603
    cmp qword [rax + 8], 0x4000    ; Capacité maximale du payload rdata.
    ja fail_e603
    cmp qword [rbp + F_TEXT_OFFSET], 0x8000 ; Compteur texte borné même après align.
    ja fail_e603
%ifdef B112_EXPECT_TEXT_OFFSET
    mov rax, B112_EXPECT_TEXT_OFFSET
    cmp [rbp + F_TEXT_OFFSET], rax ; Oracle test-only de la mesure après alignement.
    jne fail_e603
%endif
    call validate_entry_descriptor
    test eax, eax
    jnz fail_e603
%ifdef PRODUCE_PE
    mov r10, [rbp + F_WORK]
    cmp qword [r10 + 0x110], 0
    je producer_pass_complete
    call load_semantic_error
    jmp fail_positioned
%else
    call resolve_source_fixups
    test eax, eax
    jz cleanup
    mov edi, eax
    mov rax, r8
    jmp fail_positioned
%endif

semantic_eof:
%ifdef PRODUCE_PE
    mov rax, [rbp + F_WORK]
    mov qword [rax + 0x108], 0
    cmp qword [rbp + F_SEM_STAGE], AFTER_ENTRY
    jb .state_check
    call validate_entry_fields
    test eax, eax
    jnz fail_e603
    call validate_output_entry
    test eax, eax
    jnz .deferred_error
    call resolve_source_fixups
    test eax, eax
    jz .state_check
.deferred_error:
    mov edi, eax
    mov rax, r8
    jmp fail_positioned
.state_check:
%endif
    cmp qword [rbp + F_SEM_STAGE], IN_DATA ; Les trois en-têtes sont maintenant obligatoires.
    je semantic_success
    cmp qword [rbp + F_SEM_STAGE], IN_RDATA ; La section .data manque.
    je fail_e200_eof
    cmp qword [rbp + F_SEM_STAGE], IN_TEXT ; La section rdata obligatoire manque.
    je fail_e200_eof
    cmp qword [rbp + F_SEM_STAGE], AFTER_ENTRY ; L'entrée existe mais .text manque.
    je fail_e200_eof               ; Signaler l'en-tête manquant à EOF.
    cmp qword [rbp + F_SEM_STAGE], CONST_OR_ENTRY
    je fail_e302_eof
    cmp qword [rbp + F_SEM_STAGE], EXPECT_FORMAT
    je fail_e200                    ; aucune ligne format : compatibilité B1.1
    cmp qword [rbp + F_SEM_STAGE], EXPECT_ABI
    jne fail_e603
fail_e200_eof:                     ; Diagnostic EOF commun à abi ou .text manquants.
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

semantic_e200_at_begin:
    mov r11, [rbp + F_SEM_BEGIN]
    lea rdx, [r11 + 1]
    mov rcx, [rbp + F_SEM_START]
    add rcx, r11
    jc fail_e603
    mov rax, [rbp + F_SEM_LINE]
    mov edi, 200
    jmp fail_positioned

fail_e302_eof:
    mov edi, 302
    mov rax, [rbp + F_LINES_DONE]
    inc rax
    mov edx, 1
    mov rcx, [rbp + F_SOURCE_LEN]
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
%ifdef PRODUCE_PE
    mov eax, 100
    lea rcx, [r11 + 1]
    call record_physical_error
    xor ecx, ecx                 ; Dernière ligne virtuelle, aucune écriture/modification de SRC.
    jmp scan_eol
%else
    mov edi, 100
    mov rax, r9
    lea rdx, [r11 + 1]
    mov rcx, [rbp + F_SOURCE_LEN]
    jmp fail_positioned

%endif
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
%ifdef PRODUCE_PE
    mov r10, [rbp + F_WORK]
    cmp qword [r10 + 0x100], 1
    jne .publish
    cmp edi, 600
    jae .publish
    call remember_semantic_error
    cmp qword [r10 + 0x108], 1
    jne .pending
    call rollback_semantic_transaction
    jmp semantic_next_record
.pending:
    call load_semantic_error
.publish:
%endif
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
%ifdef ADDR_SNAPSHOT
    cmp qword [rbp + F_DIAG], 0
    jne .address_snapshot_done
    call verify_address_snapshot
    test eax, eax
    jz .address_snapshot_done
    mov qword [rbp + F_DIAG], 603
    SET_ZERO_POSITION
    mov rax, EFI_LOAD_ERROR
    mov [rbp + F_EXIT_STATUS], rax
.address_snapshot_done:
%endif
%ifdef FIXUP_SNAPSHOT
    cmp qword [rbp + F_DIAG], 0
    jne .fixup_snapshot_done
    call verify_fixup_snapshot
    test eax, eax
    jz .fixup_snapshot_done
    mov qword [rbp + F_DIAG], 603
    SET_ZERO_POSITION
    mov rax, EFI_LOAD_ERROR
    mov [rbp + F_EXIT_STATUS], rax
.fixup_snapshot_done:
%endif
%ifdef INSN_SNAPSHOT
    cmp qword [rbp + F_DIAG], 0
    jne .instruction_snapshot_done
    call verify_instruction_snapshot
    test eax, eax
    jz .instruction_snapshot_done
    mov qword [rbp + F_DIAG], 603
    SET_ZERO_POSITION
    mov rax, EFI_LOAD_ERROR
    mov [rbp + F_EXIT_STATUS], rax
.instruction_snapshot_done:
%endif
%ifdef TEXT_SNAPSHOT
    cmp qword [rbp + F_DIAG], 0
    jne .text_snapshot_done
    call verify_text_snapshot
    test eax, eax
    jz .text_snapshot_done
    mov qword [rbp + F_DIAG], 603
    SET_ZERO_POSITION
    mov rax, EFI_LOAD_ERROR
    mov [rbp + F_EXIT_STATUS], rax
.text_snapshot_done:
%endif
%ifdef SCALAR_SNAPSHOT
    cmp qword [rbp + F_DIAG], 0
    jne .scalar_snapshot_done
    call verify_scalar_snapshot   ; Compteurs et octets de la dernière ligne validée.
    test eax, eax
    jz .scalar_snapshot_done
    mov qword [rbp + F_DIAG], 603
    SET_ZERO_POSITION
    mov rax, EFI_LOAD_ERROR
    mov [rbp + F_EXIT_STATUS], rax
.scalar_snapshot_done:
%endif
%ifdef B111_SNAPSHOT
    cmp qword [rbp + F_DIAG], 0
    jne .b111_snapshot_done
    call b111_verify_symbols       ; Les trois records doivent être byte-exacts.
    test eax, eax
    jz .b111_snapshot_done
    mov qword [rbp + F_DIAG], 603
    SET_ZERO_POSITION
    mov rax, EFI_LOAD_ERROR
    mov [rbp + F_EXIT_STATUS], rax
.b111_snapshot_done:
%endif
%ifdef B15_SNAPSHOT_MULTI
    ; Build de preuve hérité : B15-04 ne réussit que si les trois
    ; records internes sont byte-exacts avant cleanup.
    cmp qword [rbp + F_DIAG], 0
    jne .snapshot_done
    call verify_b15_multi_snapshot
    test eax, eax
    jz .snapshot_done
    mov qword [rbp + F_DIAG], 603
    SET_ZERO_POSITION
    mov rax, EFI_LOAD_ERROR
    mov [rbp + F_EXIT_STATUS], rax
.snapshot_done:
%endif
%ifdef B16_SNAPSHOT_CASE
    ; Build de preuve uniquement : le succès exige les records B1.6 byte-exacts.
    cmp qword [rbp + F_DIAG], 0
    jne .b16_snapshot_done
    call verify_b16_snapshot
    test eax, eax
    jz .b16_snapshot_done
    mov qword [rbp + F_DIAG], 603
    SET_ZERO_POSITION
    mov rax, EFI_LOAD_ERROR
    mov [rbp + F_EXIT_STATUS], rax
.b16_snapshot_done:
%endif
%ifdef B17_SNAPSHOT_CASE
    ; Build de preuve uniquement : le succès exige le descripteur entry
    ; byte-exact (cinq qwords contigus) avant toute libération.
    cmp qword [rbp + F_DIAG], 0
    jne .b17_snapshot_done
    call verify_b17_snapshot
    test eax, eax
    jz .b17_snapshot_done
    mov qword [rbp + F_DIAG], 603
    SET_ZERO_POSITION
    mov rax, EFI_LOAD_ERROR
    mov [rbp + F_EXIT_STATUS], rax
.b17_snapshot_done:
%endif
%ifdef B18_SNAPSHOT
    cmp qword [rbp + F_DIAG], 0    ; Une erreur source déjà trouvée reste prioritaire.
    jne .b18_snapshot_done
    call verify_b18_snapshot       ; Observer les six qwords avant les libérations.
    test eax, eax
    jz .b18_snapshot_done
    mov qword [rbp + F_DIAG], 603 ; Un seul octet différent interdit le succès.
    SET_ZERO_POSITION             ; L'incohérence interne n'a pas de position source.
    mov rax, EFI_LOAD_ERROR
    mov [rbp + F_EXIT_STATUS], rax
.b18_snapshot_done:
%endif
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
%ifdef PRODUCE_PE
    lea rdx, [rel producer_success]
%else
    lea rdx, [rel success_b112]
%endif
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

; --- B1.8 : helpers feuilles, registres volatils uniquement ----------------
; b18_lex_header(rcx=ligne, rdx=debut, r8=fin exclusive).
; Sortie : rax=0, edx=nombre; ou rax=index+1, edx=101. RCX conservé.
; Chaque record de WORK+0x1C400 vaut deux qwords : index et longueur.
; B1.11 : IDENT WS? ':' seul, reconnu avant les mots-clés de statements.
; Si aucun ':' ne suit le premier mot, RCX/RDX/R8 sont conservés et EAX=0.
; Sinon : EAX=1 si inséré, ou EAX=code et RDX=colonne physique.
; Reconnaître align sans confondre alignment/align8 avec le mot-clé.
; EAX=0 : arguments conservés; EAX=1 : compteur mis à jour; sinon code/colonne.
; Famille de données scalaire. EAX=0 si le mot-clé n'est pas reconnu
; (RCX/RDX/R8 conservés), EAX=1 après validation, sinon code/colonne.
; ascii/utf16z/zero partagent validation de section, publication et capacités.
; Encodage borné dans le scratch : aucun octet de plus qu'une instruction x86.
%macro INSN_BYTE 1
    cmp r14, 15
    jae .internal
    mov byte [r15 + r14], %1
    inc r14
%endmacro

%macro INSN_REG 1
    mov rdx, [rdi + 16 * %1]
    lea rcx, [rsi + rdx]
    mov rdx, [rdi + 16 * %1 + 8]
    call parse_gpr64
%endmacro

%macro INSN_COMMA 1
    cmp rbx, %1 + 1
    jb .missing
    mov rdx, [rdi + 16 * %1]
    cmp qword [rdi + 16 * %1 + 8], 1
    jne .syntax_at_rdx
    cmp byte [rsi + rdx], ','
    jne .syntax_at_rdx
%endmacro

; Branches rel32 : la pass 1 garde la tranche de nom, même pour un label futur.
try_branch_instruction:
    call find_branch_instruction
    test rax, rax
    jz .no
    cmp qword [rbp + F_SEM_STAGE], IN_TEXT
    jne .data_forbidden
    push rbx
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    sub rsp, 0x40
    mov r12, rax
    mov rsi, rcx
    mov [rsp + 0x20], rcx
    mov [rsp + 0x28], rdx
    mov [rsp + 0x30], r8
    call b18_lex_header
    test rax, rax
    jnz .lex_error
    mov rbx, rdx
    mov rdi, [rbp + F_WORK]
    add rdi, 0x1C400
    cmp rbx, 2
    jb .missing
    ; call GPR est traité par l'encodeur registre, sans perdre la tranche de ligne.
    cmp byte [r12 + 2], 0xE8
    jne .name
    INSN_REG 1
    test eax, eax
    js .name
    mov rcx, [rsp + 0x20]
    mov rdx, [rsp + 0x28]
    mov r8, [rsp + 0x30]
    xor eax, eax
    jmp .out
.name:
    mov rax, [rdi]
    add rax, [rdi + 8]
    mov rdx, [rdi + 16]
    cmp rdx, rax
    jbe .syntax
    mov rcx, rsi
    add rcx, rdx
    mov rdx, [rdi + 24]
    call validate_symbol_spelling
    test eax, eax
    jnz .bad_name
    cmp rbx, 2
    ja .extra
    mov r15, [rbp + F_WORK]
    add r15, 0x1C500
    xor r14d, r14d
    cmp byte [r12 + 1], 2
    jne .opcode
    INSN_BYTE 0x0F
.opcode:
    mov al, [r12 + 2]
    INSN_BYTE al
    mov r13, r14              ; Offset du champ rel32 dans l'instruction.
    INSN_BYTE 0
    INSN_BYTE 0
    INSN_BYTE 0
    INSN_BYTE 0
    lea rcx, [rdi + 16]
    mov rdx, [rbp + F_TEXT_OFFSET]
    lea r8, [rdx + r14]
    add rdx, r13
    mov r9d, 1
    call record_source_fixup
    test eax, eax
    jnz .out
    call commit_staged_instruction
    jmp .out
.extra:
    mov rdx, [rdi + 32]
.syntax:
    inc rdx
    mov eax, 201
    jmp .out
.bad_name:
    mov rdx, [rdi + 16]
    inc rdx
    mov eax, 400
    jmp .out
.missing:
    mov rdx, [rsp + 0x30]
    inc rdx
    mov eax, 201
    jmp .out
.lex_error:
    xchg rax, rdx
    jmp .out
.internal:
    mov eax, 603
    xor edx, edx
.out:
    add rsp, 0x40
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    ret
.data_forbidden:
    inc rdx
    mov eax, 202
    ret
.no:
    xor eax, eax
    ret

; Un nom de symbole est ASCII, sensible à la casse, borné à 31 octets.
; RCX=octets, RDX=longueur, EAX=0 valide ou 1 invalide.
validate_symbol_spelling:
    test rdx, rdx
    jz .bad
    cmp rdx, 31
    ja .bad
    xor r8d, r8d
.next:
    mov al, [rcx + r8]
    cmp al, '_'
    je .advance
    or al, 0x20
    cmp al, 'a'
    jb .digit
    cmp al, 'z'
    jbe .advance
.digit:
    test r8, r8
    jz .bad
    cmp al, '0'
    jb .bad
    cmp al, '9'
    ja .bad
.advance:
    inc r8
    cmp r8, rdx
    jb .next
    xor eax, eax
    ret
.bad:
    mov eax, 1
    ret

; RCX=record token, RDX=site texte, R8=octet suivant, R9=kind branche/RIP.
; Toute capacité est contrôlée avant la première écriture de record.
record_source_fixup:
    mov r10, [rbp + F_WORK]
    mov rax, [r10 + 0x40]
    cmp rax, 2048
    jae .capacity
    imul rax, rax, 24
    lea r10, [r10 + rax + 0x6400]
%ifdef PRODUCE_PE
    SAVE_PASS_RECORD r10, 24
%endif
    mov [r10 + 4], edx
    mov [r10 + 8], r8d
    mov [r10 + 18], r9w
    mov r11, [rcx]
    lea rax, [r11 + 1]
    mov [r10 + 14], ax
    add r11, [rbp + F_SEM_START]
    mov [r10], r11d
    mov rax, [rcx + 8]
    mov [r10 + 16], ax
    mov rax, [rbp + F_SEM_LINE]
    mov [r10 + 12], ax
%ifdef PRODUCE_PE
    CHECK_PASS_RECORD r10, 20, .inconsistent
    mov rax, [rbp + F_WORK]
    cmp qword [rax + 0x100], 2
    je .keep_resolved
%endif
    mov dword [r10 + 20], 0
%ifdef PRODUCE_PE
.keep_resolved:
%endif
    mov r10, [rbp + F_WORK]
    inc qword [r10 + 0x40]
    xor eax, eax
    ret
%ifdef PRODUCE_PE
.inconsistent:
    mov rdx, [rcx]
    inc rdx
    mov eax, 600
    ret
%endif
.capacity:
    mov rdx, [rcx]
    inc rdx
    mov eax, 500
    ret

; RCX=token du nom, RDX=site relatif de section, R9=section 2/3.
record_data_reference:
    mov r10, [rbp + F_WORK]
    mov rax, [r10 + 0x60]
    cmp rax, 512
    jae .capacity
    shl rax, 4
    lea r10, [r10 + rax + 0x12400]
%ifdef PRODUCE_PE
    SAVE_PASS_RECORD r10, 16
%endif
    mov [r10 + 4], edx
    mov [r10 + 14], r9w
    mov rax, [rcx]
    lea rdx, [rax + 1]
    mov [r10 + 10], dx
    add rax, [rbp + F_SEM_START]
    mov [r10], eax
    mov rax, [rcx + 8]
    mov [r10 + 12], ax
    mov rax, [rbp + F_SEM_LINE]
    mov [r10 + 8], ax
%ifdef PRODUCE_PE
    CHECK_PASS_RECORD r10, 16, .inconsistent
%endif
    mov r10, [rbp + F_WORK]
    inc qword [r10 + 0x60]
    xor eax, eax
    ret
%ifdef PRODUCE_PE
.inconsistent:
    mov rdx, [rcx]
    inc rdx
    mov eax, 600
    ret
%endif
.capacity:
    mov rdx, [rcx]
    inc rdx
    mov eax, 500
    ret

; Les addr ne sont pas des constantes : ils doivent nommer un label de section.
resolve_data_references:
    push rbx
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    sub rsp, 0x20
    mov rsi, [rbp + F_WORK]
    mov rbx, [rsi + 0x60]
    add rsi, 0x12400
.next:
    test rbx, rbx
    jz .ok
    mov ecx, [rsi]
    add rcx, [rbp + F_SRC]
    movzx edx, word [rsi + 12]
    call lookup_symbol_name
    test rax, rax
    jz .absent
    movzx edx, word [rax + 46]
    and edx, 0x6F
    cmp edx, 0x22
    je .valid
    cmp edx, 0x42
    je .valid
    cmp edx, 0x62
    jne .absent
.valid:
    add rsi, 16
    dec rbx
    jmp .next
.ok:
    xor eax, eax
    jmp .out
.absent:
    mov eax, 301
    mov ecx, [rsi]
    movzx edx, word [rsi + 10]
    movzx r8d, word [rsi + 8]
.out:
    add rsp, 0x20
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    ret

; Recherche exacte dans la table partagée, constante ou label.
; RCX=nom source, RDX=longueur; RAX=record ou zéro.
lookup_symbol_name:
    mov r8, [rbp + F_WORK]
    add r8, 0x400
    mov r9, [rbp + F_SYMBOL_COUNT]
.record:
    test r9, r9
    jz .absent
    xor r10d, r10d
.byte:
    cmp r10, rdx
    jae .length
    mov r11b, [rcx + r10]
    cmp r11b, [r8 + r10]
    jne .next
    inc r10
    jmp .byte
.length:
    cmp byte [r8 + rdx], 0
    jne .next
    mov rax, r8
    ret
.next:
    add r8, 48
    dec r9
    jmp .record
.absent:
    xor eax, eax
    ret

; Mesure de layout et résolution après toutes les définitions de labels.
; EAX=0 ou erreur; R8=ligne, RDX=colonne, RCX=offset source en cas d'erreur.
resolve_source_fixups:
    push rbx
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    sub rsp, 0x20
    mov r15, [rbp + F_WORK]
    mov rax, [rbp + F_TEXT_OFFSET]
    add rax, 0xFFF
    and rax, -0x1000
    add rax, 0x1000
    mov [r15 + 0x50], rax
    mov rdx, [r15 + 8]
    add rdx, 0xFFF
    and rdx, -0x1000
    add rdx, rax
    mov [r15 + 0x58], rdx
    mov rbx, [r15 + 0x40]
    lea rsi, [r15 + 0x6400]
    mov r14, [rbp + F_SRC]
.next:
    test rbx, rbx
    jz .ok
    mov ecx, [rsi]
    add rcx, r14
    movzx edx, word [rsi + 16]
    call lookup_symbol_name
    test rax, rax
    jz .absent
    movzx ecx, word [rax + 46]
    and ecx, 0x6F
    mov rdx, [rax + 32]
    cmp ecx, 0x22
    je .text
    cmp word [rsi + 18], 1
    je .absent
    cmp ecx, 0x42
    je .rdata
    cmp ecx, 0x62
    jne .absent
    add rdx, [r15 + 0x58]
    jmp .relative
.rdata:
    add rdx, [r15 + 0x50]
    jmp .relative
.text:
    add rdx, 0x1000
.relative:
    mov eax, [rsi + 8]
    add rax, 0x1000
    sub rdx, rax
    movsxd rax, edx
    cmp rax, rdx
    jne .range
%ifdef PRODUCE_PE
    cmp qword [r15 + 0x100], 2
    jne .first_resolution
    cmp [rsi + 20], edx
    jne .inconsistent
.first_resolution:
%endif
    mov [rsi + 20], edx
    add rsi, 24
    dec rbx
    jmp .next
.ok:
    call resolve_data_references
    jmp .out
.absent:
    mov eax, 301
    jmp .position
%ifdef PRODUCE_PE
.inconsistent:
    mov eax, 600
    jmp .position
%endif
.range:
    mov eax, 401
.position:
    mov ecx, [rsi]
    movzx edx, word [rsi + 14]
    movzx r8d, word [rsi + 12]
.out:
    add rsp, 0x20
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    ret

; Groupe mémoire : une description commune adresse les neuf formes S0.
; Le descripteur scratch de l'adresse est séparé des octets de sortie.
try_memory_instruction:
    call find_memory_instruction
    test rax, rax
    jz .no
    cmp qword [rbp + F_SEM_STAGE], IN_TEXT
    jne .data_forbidden
    push rbx
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    sub rsp, 0x40
    mov r12, rax
    mov rsi, rcx
    mov [rsp + 0x30], r8
    call b18_lex_header
    test rax, rax
    jnz .lex_error
    mov rbx, rdx
    mov rdi, [rbp + F_WORK]
    add rdi, 0x1C400
    cmp rbx, 2
    jb .missing
    mov rax, [rdi]
    add rax, [rdi + 8]
    mov rdx, [rdi + 16]
    cmp rdx, rax
    jbe .syntax_at_rdx
    mov r9d, 1
    test byte [r12 + 3], 1
    jnz .memory
    INSN_REG 1
    test eax, eax
    js .bad_reg1
    mov r13d, eax
    INSN_COMMA 2
    mov r9d, 3
.memory:
    cmp r9, rbx
    jae .missing
    mov [rsp + 0x20], r9       ; Position du crochet ouvrant dans le flux token.
    mov rdx, r9
    shl rdx, 4
    add rdx, rdi
    mov rcx, rsi
    mov r8, rbx
    sub r8, r9
    mov r9, [rsp + 0x30]
    call parse_memory_operand
    test eax, eax
    jnz .out
    mov rax, [rbp + F_WORK]
    mov r9, [rax + 0x1C900 + 40]
    add r9, [rsp + 0x20]
    test byte [r12 + 3], 1
    jz .check_end
    cmp r9, rbx
    jae .missing
    mov rax, r9
    shl rax, 4
    mov rdx, [rdi + rax]
    cmp qword [rdi + rax + 8], 1
    jne .syntax_at_rdx
    cmp byte [rsi + rdx], ','
    jne .syntax_at_rdx
    inc r9
    cmp r9, rbx
    jae .missing
    mov [rsp + 0x20], r9
    shl r9, 4
    mov rcx, [rdi + r9]
    add rcx, rsi
    mov rdx, [rdi + r9 + 8]
    call parse_gpr64
    mov r9, [rsp + 0x20]
    test eax, eax
    js .bad_reg_at_r9
    mov r13d, eax
    inc r9
.check_end:
    cmp r9, rbx
    jb .extra
    mov r10, [rbp + F_WORK]
    lea r15, [r10 + 0x1C500]
    add r10, 0x1C900
    xor r14d, r14d
    test byte [r12 + 3], 2
    jz .rex
    INSN_BYTE 0x66
.rex:
    mov eax, r13d
    shr eax, 3
    shl eax, 2               ; Registre de donnée -> extension REX.R.
    or al, [r12 + 1]
    or al, [r10 + 8]         ; Base et index -> REX.B et REX.X.
    INSN_BYTE al
    test byte [r12 + 3], 4
    jz .opcode
    INSN_BYTE 0x0F
.opcode:
    mov al, [r12 + 2]
    INSN_BYTE al
    mov eax, r13d
    and eax, 7
    shl eax, 3
    or al, [r10]
    INSN_BYTE al
    cmp qword [r10 + 16], -1
    je .displacement
    mov al, [r10 + 16]
    INSN_BYTE al
.displacement:
    mov r8, [r10 + 32]
    mov rax, [r10 + 24]
.disp_byte:
    test r8, r8
    jz .commit
    INSN_BYTE al
    shr rax, 8
    dec r8
    jmp .disp_byte
.commit:
    mov rax, [rbp + F_WORK]
    mov rcx, [rax + 0x1C900 + 48]
    test rcx, rcx
    jz .commit_bytes
    mov r8, [rbp + F_TEXT_OFFSET]
    add r8, r14
    lea rdx, [r8 - 4]
    mov r9d, 2
    call record_source_fixup
    test eax, eax
    jnz .out
.commit_bytes:
    call commit_staged_instruction
    jmp .out
.extra:
    shl r9, 4
    mov rdx, [rdi + r9]
.syntax_at_rdx:
    inc rdx
    mov eax, 201
    jmp .out
.missing:
    mov rdx, [rsp + 0x30]
    inc rdx
    mov eax, 201
    jmp .out
.bad_reg1:
    mov r9d, 1
.bad_reg_at_r9:
    shl r9, 4
    mov rdx, [rdi + r9]
    inc rdx
    mov eax, 400
    jmp .out
.lex_error:
    xchg rax, rdx
    jmp .out
.internal:
    mov eax, 603
    xor edx, edx
.out:
    add rsp, 0x40
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    ret
.data_forbidden:
    inc rdx
    mov eax, 202
    ret
.no:
    xor eax, eax
    ret

; RCX=ligne, RDX=premier token mémoire, R8=nombre restant, R9=fin logique.
; EAX=0 ou code, RDX=colonne si erreur. Descripteur WORK+1C900 :
; mod/rm, REX.BX, SIB ou -1, déplacement, largeur, nombre de tokens consommés.
parse_memory_operand:
    push rbx
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    sub rsp, 0x50
    mov rsi, rcx
    mov rdi, rdx
    mov rbx, r8
    mov [rsp + 0x20], r9
    mov rax, [rbp + F_WORK]
    mov qword [rax + 0x1C900 + 48], 0
    mov qword [rsp + 0x28], 0
    mov qword [rsp + 0x30], 0
    xor r12d, r12d
    mov r14, -1
    xor r15d, r15d
    test rbx, rbx
    jz .missing
    mov rdx, [rdi]
    cmp qword [rdi + 8], 1
    jne .syntax
    cmp byte [rsi + rdx], '['
    jne .syntax
    inc r12
    cmp r12, rbx
    jae .missing
    mov rdx, [rdi + 16]
    lea rcx, [rsi + rdx]
    cmp qword [rdi + 24], 3
    jne .base
    cmp word [rcx], 'ri'
    jne .base
    cmp byte [rcx + 2], 'p'
    je .rip
.base:
    mov rdx, [rdi + 24]
    call parse_gpr64
    test eax, eax
    js .address
    mov r13d, eax
    inc r12
.tail:
    cmp r12, rbx
    jae .missing
    mov rax, r12
    shl rax, 4
    mov rdx, [rdi + rax]
    cmp qword [rdi + rax + 8], 1
    jne .syntax
    mov al, [rsi + rdx]
    cmp al, ']'
    je .close
    cmp al, '+'
    je .positive
    cmp al, '-'
    jne .syntax
    mov qword [rsp + 0x30], 1
    jmp .after_sign
.positive:
    mov qword [rsp + 0x30], 0
.after_sign:
    inc r12
    cmp r12, rbx
    jae .missing
    ; Un '*' après le premier terme sélectionne l'alternative indexée.
    lea rax, [r12 + 1]
    cmp rax, rbx
    jae .parse_disp
    shl rax, 4
    cmp qword [rdi + rax + 8], 1
    jne .parse_disp
    mov rdx, [rdi + rax]
    cmp byte [rsi + rdx], '*'
    jne .parse_disp
    cmp r14, -1
    jne .syntax
    cmp qword [rsp + 0x30], 0
    jne .syntax
    mov rax, r12
    shl rax, 4
    mov rcx, [rdi + rax]
    add rcx, rsi
    mov rdx, [rdi + rax + 8]
    call parse_gpr64
    test eax, eax
    js .address
    cmp eax, 4
    je .address
    cmp eax, 12
    je .address
    mov r14d, eax
    add r12, 2
    cmp r12, rbx
    jae .missing
    mov rax, r12
    shl rax, 4
    cmp qword [rdi + rax + 8], 1
    jne .address
    mov rdx, [rdi + rax]
    mov al, [rsi + rdx]
    xor r15d, r15d
    cmp al, '1'
    je .scaled
    inc r15d
    cmp al, '2'
    je .scaled
    inc r15d
    cmp al, '4'
    je .scaled
    inc r15d
    cmp al, '8'
    jne .address
.scaled:
    inc r12
    jmp .tail
.parse_disp:
    mov rax, r12
    shl rax, 4
    mov rdx, [rdi + rax]
    mov cl, [rsi + rdx]
    cmp cl, '-'
    je .syntax
    cmp cl, '+'
    je .syntax
    cmp cl, ']'
    je .syntax
    mov [rsp + 0x38], rdx
    lea rdx, [rdi + rax]
    mov rcx, rsi
    mov r8d, 1
    mov r9d, 1
    call parse_operand_value
    test r10d, r10d
    jnz .value_error
    test r8, r8
    jnz .range
    ; Les constantes ont une magnitude i32 non négative, même après '-'.
    mov rdx, [rsp + 0x38]
    mov cl, [rsi + rdx]
    cmp cl, '0'
    jb .constant_range
    cmp cl, '9'
    ja .constant_range
    mov edx, 0x7FFFFFFF
    cmp qword [rsp + 0x30], 0
    je .bound
    inc rdx
    jmp .bound
.constant_range:
    mov edx, 0x7FFFFFFF
.bound:
    cmp rax, rdx
    ja .range
    cmp qword [rsp + 0x30], 0
    je .save_disp
    neg rax
.save_disp:
    mov [rsp + 0x28], rax
    inc r12
    cmp r12, rbx
    jae .missing
    mov rax, r12
    shl rax, 4
    mov rdx, [rdi + rax]
    cmp qword [rdi + rax + 8], 1
    jne .syntax
    cmp byte [rsi + rdx], ']'
    jne .syntax
.close:
    mov r10, [rbp + F_WORK]
    add r10, 0x1C900
    inc r12
    mov [r10 + 40], r12
    mov rax, [rsp + 0x28]
    mov [r10 + 24], rax
    xor r8d, r8d
    xor r9d, r9d
    test rax, rax
    jnz .nonzero
    mov ecx, r13d
    and ecx, 7
    cmp ecx, 5
    jne .have_mod
.disp8:
    mov r8d, 0x40
    mov r9d, 1
    jmp .have_mod
.nonzero:
    cmp rax, -128
    jl .disp32
    cmp rax, 127
    jle .disp8
.disp32:
    mov r8d, 0x80
    mov r9d, 4
.have_mod:
    mov [r10 + 32], r9
    mov eax, r13d
    shr eax, 3
    mov [r10 + 8], rax
    mov qword [r10 + 16], -1
    mov eax, r13d
    and eax, 7
    cmp r14, -1
    jne .sib
    cmp eax, 4
    je .sib
    or eax, r8d
    mov [r10], rax
    jmp .ok
.sib:
    or r8d, 4
    mov [r10], r8
    mov ecx, 4                    ; Index SIB=4 signifie absence quand X=0.
    cmp r14, -1
    je .sib_low
    mov ecx, r14d
    shr ecx, 3
    shl ecx, 1
    or [r10 + 8], rcx
    mov ecx, r14d
    and ecx, 7
.sib_low:
    shl ecx, 3
    or eax, ecx
    shl r15d, 6
    or eax, r15d
    mov [r10 + 16], rax
.ok:
    xor eax, eax
    xor edx, edx
    jmp .out
.address:
    mov eax, 402
    jmp .at_current
.range:
    mov eax, 401
    jmp .at_current
.rip:
    inc r12
    cmp r12, rbx
    jae .missing
    mov rdx, [rdi + 32]
    cmp qword [rdi + 40], 1
    jne .syntax
    cmp byte [rsi + rdx], '+'
    jne .syntax
    inc r12
    cmp r12, rbx
    jae .missing
    mov rcx, [rdi + 48]
    add rcx, rsi
    mov rdx, [rdi + 56]
    call validate_symbol_spelling
    test eax, eax
    jnz .bad_rip_name
    inc r12
    cmp r12, rbx
    jae .missing
    mov rdx, [rdi + 64]
    cmp qword [rdi + 72], 1
    jne .syntax
    cmp byte [rsi + rdx], ']'
    jne .syntax
    mov r10, [rbp + F_WORK]
    add r10, 0x1C900
    mov qword [r10], 5
    mov qword [r10 + 8], 0
    mov qword [r10 + 16], -1
    mov qword [r10 + 24], 0
    mov qword [r10 + 32], 4
    mov qword [r10 + 40], 5
    lea rax, [rdi + 48]
    mov [r10 + 48], rax
    jmp .ok
.bad_rip_name:
    mov eax, 400
    jmp .at_current
.at_current:
    mov rdx, r12
    shl rdx, 4
    mov rdx, [rdi + rdx]
    inc rdx
    jmp .out
.syntax:
    inc rdx
    mov eax, 201
    jmp .out
.missing:
    mov rdx, [rsp + 0x20]
    inc rdx
    mov eax, 201
    jmp .out
.value_error:
    mov eax, r10d
    mov rdx, r11
.out:
    add rsp, 0x50
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    ret

try_register_instruction:
    call find_register_instruction ; RCX/RDX/R8 inchangés, RAX=description ou zéro.
    test rax, rax
    jz .no
    cmp qword [rbp + F_SEM_STAGE], IN_TEXT
    jne .data_forbidden
    push rbx
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    sub rsp, 0x40
    mov r12, rax
    mov rsi, rcx
    mov [rsp + 0x30], r8          ; Fin logique pour les opérandes absents.
    mov rax, [rbp + F_WORK]
    mov qword [rax + 0x20], 0
    mov qword [rax + 0x28], 0
    call b18_lex_header
    test rax, rax
    jnz .lex_error
    mov rbx, rdx
    mov rdi, [rbp + F_WORK]
    add rdi, 0x1C400
    cmp byte [r12 + 1], 0
    je .no_operands
    cmp rbx, 2
    jb .missing
    mov rax, [rdi]
    add rax, [rdi + 8]
    mov rdx, [rdi + 16]
    cmp rdx, rax
    jbe .syntax_at_rdx
    INSN_REG 1
    test eax, eax
    js .bad_operand1
    mov r13d, eax                 ; Registre destination, ou registre de l'opération unaire.
    cmp byte [r12 + 1], 2
    jbe .one_operand
    INSN_COMMA 2
    cmp rbx, 4
    jb .missing
    INSN_REG 3
    cmp byte [r12 + 1], 6
    je .shift
    test eax, eax
    js .non_register_source
    mov [rsp + 0x28], rax
    cmp byte [r12 + 1], 7
    je .imul
    cmp rbx, 4
    ja .extra4
    mov r11d, 0                   ; Forme registre/registre.
    jmp .encode
.non_register_source:
    cmp byte [r12 + 1], 5
    jae .bad_operand3             ; test et imul exigent un registre source.
    mov rdx, [rdi + 48]
    cmp byte [rsi + rdx], '['
    je .bad_operand3              ; mov mémoire n'est pas une forme S0.
    jmp .immediate3
.shift:
    test eax, eax
    jns .bad_operand3
.immediate3:
    mov r15d, 3
    jmp .parse_immediate
.imul:
    INSN_COMMA 4
    cmp rbx, 6
    jb .missing
    INSN_REG 5
    test eax, eax
    jns .bad_operand5
    mov r15d, 5
.parse_immediate:
    mov rax, r15
    shl rax, 4
    lea rdx, [rdi + rax]
    mov rcx, rsi
    mov r8, rbx
    sub r8, r15
    mov r9d, 1                    ; Constantes autorisées sauf mov imm64.
    cmp byte [r12 + 1], 3
    jne .immediate_call
    xor r9d, r9d
.immediate_call:
    call parse_operand_value
    test r10d, r10d
    jnz .immediate_error
    mov [rsp + 0x20], rax
    mov [rsp + 0x38], r8
    add r9, r15
    cmp byte [r12 + 1], 3
    je .immediate_valid
    cmp byte [r12 + 1], 6
    je .shift_range
    test r8d, r8d
    jnz .negative_imm32
    cmp rax, 0x7FFFFFFF           ; Pas de réinterprétation d'un grand entier positif.
    ja .range
    jmp .immediate_valid
.negative_imm32:
    cmp rax, -2147483648
    jl .range
    jmp .immediate_valid
.shift_range:
    cmp rax, 63
    ja .range
.immediate_valid:
    cmp rbx, r9                  ; Une plage invalide située avant l'extra gagne.
    ja .extra_at_r9
    jb .missing
    mov r11d, 1                   ; Forme immédiate.
    jmp .encode
.no_operands:
    cmp rbx, 1
    ja .extra1
    xor r11d, r11d
    jmp .encode
.one_operand:
    cmp rbx, 2
    ja .extra2
    xor r11d, r11d
.encode:
    xor r14d, r14d
    mov r15, [rbp + F_WORK]
    add r15, 0x1C500
    cmp byte [r12 + 1], 0
    je .encode_none
    cmp byte [r12 + 1], 1
    je .encode_stack
    cmp byte [r12 + 1], 2
    je .encode_call
    cmp byte [r12 + 1], 7
    je .encode_imul
    test r11d, r11d
    jz .encode_rr
    cmp byte [r12 + 1], 3
    je .encode_mov_imm
    jmp .encode_small_imm
.encode_none:
    mov al, [r12 + 2]
    INSN_BYTE al
    jmp .commit
.encode_stack:
    cmp r13d, 8
    jb .stack_opcode
    INSN_BYTE 0x41
.stack_opcode:
    mov eax, r13d
    and eax, 7
    add al, [r12 + 2]
    INSN_BYTE al
    jmp .commit
.encode_call:
    cmp r13d, 8
    jb .call_opcode
    INSN_BYTE 0x41
.call_opcode:
    INSN_BYTE 0xFF
    mov eax, r13d
    and eax, 7
    or eax, 0xD0
    INSN_BYTE al
    jmp .commit
.encode_rr:
    mov rax, [rsp + 0x28]
    shr eax, 3
    shl eax, 2
    mov edx, r13d
    shr edx, 3
    or eax, edx
    or eax, 0x48
    INSN_BYTE al
    mov al, [r12 + 2]
    INSN_BYTE al
    mov rax, [rsp + 0x28]
    and eax, 7
    shl eax, 3
    mov edx, r13d
    and edx, 7
    or eax, edx
    or eax, 0xC0
    INSN_BYTE al
    jmp .commit
.encode_mov_imm:
    mov eax, r13d
    shr eax, 3
    or eax, 0x48
    INSN_BYTE al
    mov eax, r13d
    and eax, 7
    or eax, 0xB8
    INSN_BYTE al
    mov ecx, 8
    jmp .immediate_bytes
.encode_small_imm:
    mov eax, r13d
    shr eax, 3
    or eax, 0x48
    INSN_BYTE al
    mov eax, 0x81
    cmp byte [r12 + 1], 6
    jne .small_opcode
    mov eax, 0xC1
.small_opcode:
    INSN_BYTE al
    movzx eax, byte [r12 + 3]
    shl eax, 3
    mov edx, r13d
    and edx, 7
    or eax, edx
    or eax, 0xC0
    INSN_BYTE al
    mov ecx, 4
    cmp byte [r12 + 1], 6
    jne .immediate_bytes
    mov ecx, 1
    jmp .immediate_bytes
.encode_imul:
    mov eax, r13d
    shr eax, 3
    shl eax, 2
    mov rdx, [rsp + 0x28]
    shr edx, 3
    or eax, edx
    or eax, 0x48
    INSN_BYTE al
    INSN_BYTE 0x69
    mov eax, r13d
    and eax, 7
    shl eax, 3
    mov rdx, [rsp + 0x28]
    and edx, 7
    or eax, edx
    or eax, 0xC0
    INSN_BYTE al
    mov ecx, 4
.immediate_bytes:
    mov rax, [rsp + 0x20]
.immediate_loop:
    INSN_BYTE al
    shr rax, 8
    dec ecx
    jnz .immediate_loop
.commit:
    call commit_staged_instruction
    jmp .out
.extra1:
    mov r9d, 1
    jmp .extra_at_r9
.extra2:
    mov r9d, 2
    jmp .extra_at_r9
.extra4:
    mov r9d, 4
.extra_at_r9:
    shl r9, 4
    mov rdx, [rdi + r9]
.syntax_at_rdx:
    inc rdx
    mov eax, 201
    jmp .out
.missing:
    mov rdx, [rsp + 0x30]
    inc rdx
    mov eax, 201
    jmp .out
.bad_operand1:
    mov rdx, [rdi + 16]
    jmp .unsupported_at_rdx
.bad_operand3:
    mov rdx, [rdi + 48]
    jmp .unsupported_at_rdx
.bad_operand5:
    mov rdx, [rdi + 80]
.unsupported_at_rdx:
    inc rdx
    mov eax, 400
    jmp .out
.range:
    mov rax, r15
    shl rax, 4
    mov rdx, [rdi + rax]
    inc rdx
    mov eax, 401
    jmp .out
.capacity:
    mov rdx, [rdi]
    inc rdx
    mov eax, 500
    jmp .out
.immediate_error:
    mov eax, r10d
    mov rdx, r11
    jmp .out
.lex_error:
    xchg rax, rdx
    jmp .out
.internal:
    mov eax, 603
    xor edx, edx
.out:
    add rsp, 0x40
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    ret
.data_forbidden:
    inc rdx
    mov eax, 202
    ret
.no:
    xor eax, eax
    ret

commit_staged_instruction:
    mov rdx, [rbp + F_TEXT_OFFSET]
    mov r11, rdx
    add r11, r14
    jc .capacity
    cmp r11, 0x8000
    ja .capacity
%ifdef INSN_SNAPSHOT
    ; Preuve seulement : comparer chaque encodage au flux golden préfigé.
    cmp r11, instruction_expected_end - instruction_expected
    ja .internal
    lea r8, [rel instruction_expected]
    add r8, rdx
    xor r9d, r9d
.snapshot_byte:
    mov al, [r15 + r9]
    cmp al, [r8 + r9]
    jne .internal
    inc r9
    cmp r9, r14
    jb .snapshot_byte
%endif
%ifdef FIXUP_SNAPSHOT
    ; Capture de preuve uniquement; le producteur canonique n'écrit pas OUT en pass 1.
    mov r8, [rbp + F_OUT]
    add r8, rdx
    xor r9d, r9d
.capture:
    mov al, [r15 + r9]
    mov [r8 + r9], al
    inc r9
    cmp r9, r14
    jb .capture
%endif
    mov rax, [rbp + F_WORK]
    cmp qword [rax + 0x30], 8192
    jae .capacity
    mov r8, rax
    add r8, 0x400
    mov r9, [rbp + F_SYMBOL_COUNT]
.mark_labels:
    test r9, r9
    jz .publish
    movzx eax, word [r8 + 46]
    and eax, 0x6F
    cmp eax, 0x22
    jne .next_label
    cmp [r8 + 32], rdx
    jne .next_label
    or word [r8 + 46], 0x80       ; Ce label vise maintenant un début d'instruction explicite.
.next_label:
    add r8, 48
    dec r9
    jmp .mark_labels
.publish:
    mov [rbp + F_TEXT_OFFSET], r11
    mov rax, [rbp + F_WORK]
    inc qword [rax + 0x30]
    mov qword [rax + 0x28], 1
    mov [rax + 0x20], r14
    mov eax, 1
    ret
.capacity:
    mov rdx, [rdi]
    inc rdx
    mov eax, 500
    ret
.internal:
    mov eax, 603
    xor edx, edx
    ret

; Reconnaissance exacte du premier identifiant, arguments de ligne conservés.
find_register_instruction:
    lea r9, [rel register_instructions]
    jmp find_instruction_name
find_memory_instruction:
    lea r9, [rel memory_instructions]
    jmp find_instruction_name
find_branch_instruction:
    lea r9, [rel branch_instructions]
find_instruction_name:
    add rcx, rdx
    xor r10d, r10d
.scan:
    lea r11, [rdx + r10]
    cmp r11, r8
    jae .find
    mov al, [rcx + r10]
    cmp al, '_'
    je .advance
    cmp al, '0'
    jb .letter
    cmp al, '9'
    jbe .advance
.letter:
    or al, 0x20
    cmp al, 'a'
    jb .find
    cmp al, 'z'
    ja .find
.advance:
    inc r10
    jmp .scan
.find:
.next:
    movzx eax, byte [r9]
    test eax, eax
    jz .none
    cmp rax, r10
    jne .skip
    xor r11d, r11d
.compare:
    mov al, [r9 + r11 + 4]
    cmp al, [rcx + r11]
    jne .skip
    inc r11
    cmp r11, r10
    jb .compare
    mov rax, r9
    sub rcx, rdx
    ret
.skip:
    movzx eax, byte [r9]
    lea r9, [r9 + rax + 4]
    jmp .next
.none:
    sub rcx, rdx
    xor eax, eax
    ret

; RCX=octets, RDX=longueur. EAX=code 0..15 ou -1; feuille.
parse_gpr64:
    cmp rdx, 2
    je .two
    cmp rdx, 3
    jne .bad
    cmp word [rcx], 'r1'
    jne .low
    movzx eax, byte [rcx + 2]
    sub eax, '0'
    cmp eax, 5
    ja .bad
    add eax, 10
    ret
.two:
    cmp byte [rcx], 'r'
    jne .bad
    movzx eax, byte [rcx + 1]
    cmp eax, '8'
    jb .bad
    cmp eax, '9'
    ja .bad
    sub eax, '0'
    ret
.low:
    movzx r11d, word [rcx]
    movzx eax, byte [rcx + 2]
    shl eax, 16
    or r11d, eax
    lea r10, [rel gpr64_names]
    xor r9d, r9d
.loop:
    movzx eax, word [r10]
    movzx r8d, byte [r10 + 2]
    shl r8d, 16
    or eax, r8d
    cmp eax, r11d
    je .found
    inc r9d
    add r10, 3
    cmp r9d, 8
    jb .loop
.bad:
    mov eax, -1
    ret
.found:
    mov eax, r9d
    ret

; RCX=ligne, RDX=record token, R8=tokens restants, R9=constantes permises.
; Retour RAX=bits, R8=signe lexical, R9=tokens consommés, R10=code, R11=colonne.
parse_operand_value:
    push rbx
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    sub rsp, 0x20
    mov rsi, rcx
    mov rdi, rdx
    mov rbx, r8
    mov r12, r9
    xor r13d, r13d
    mov r14d, 1
    mov r15, [rdi]
    mov rdx, r15
    mov al, [rsi + rdx]
    cmp al, '-'
    jne .classify
    mov r13d, 1
    mov r14d, 2
    cmp rbx, 2
    jb .minus_bad
    add rdi, 16
    inc rdx
    cmp [rdi], rdx
    jne .minus_bad
    mov al, [rsi + rdx]
.classify:
    cmp al, '0'
    jb .name
    cmp al, '9'
    jbe .literal
.name:
    test r13d, r13d
    jnz .magnitude_bad
    cmp al, '_'
    je .name_valid
    or al, 0x20
    cmp al, 'a'
    jb .syntax
    cmp al, 'z'
    ja .syntax
.name_valid:
    test r12d, r12d
    jz .unsupported
    mov rcx, [rbp + F_WORK]
    add rcx, 0x400
    mov r9, [rbp + F_SYMBOL_COUNT]
    mov r8, [rdi + 8]
.lookup:
    test r9, r9
    jz .absent
    mov rdx, [rdi]
    xor r11d, r11d
.compare:
    cmp r11, r8
    jae .length
    mov al, [rsi + rdx]
    cmp al, [rcx + r11]
    jne .miss
    inc rdx
    inc r11
    jmp .compare
.length:
    cmp byte [rcx + r11], 0
    jne .miss
    movzx eax, word [rcx + 46]
    mov edx, eax
    and eax, 15
    cmp eax, 1
    jne .absent
    and edx, 0x10
    setnz r13b
    mov rax, [rcx + 32]
    jmp .success
.miss:
    add rcx, 48
    dec r9
    jmp .lookup
.literal:
    lea rcx, [rsi + rdx]
    mov rdx, [rdi + 8]
    mov r8, 0xFFFFFFFFFFFFFFFF
    test r13d, r13d
    jz .parse
    mov r8, 0x8000000000000000
.parse:
    call parse_unsigned_literal
    test r10d, r10d
    jnz .literal_error
    test r13d, r13d
    jz .success
    neg rax
.success:
    xor r10d, r10d
    xor r11d, r11d
    jmp .out
.literal_error:
    add r11, [rdi]
    inc r11
    jmp .out
.minus_bad:
    lea r11, [r15 + 2]
    mov r10d, 102
    jmp .failure
.magnitude_bad:
    lea r11, [rdx + 1]
    mov r10d, 102
    jmp .failure
.syntax:
    mov r10d, 201
    jmp .at_start
.unsupported:
    mov r10d, 400
    jmp .at_start
.absent:
    mov r10d, 301
.at_start:
    lea r11, [r15 + 1]
.failure:
    xor eax, eax
.out:
    mov r8d, r13d
    mov r9d, r14d
    add rsp, 0x20
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    ret

try_text_data:
    mov rax, r8
    sub rax, rdx
    cmp rax, 4
    jb .no
    mov r10d, 4
    mov r11d, 3
    cmp dword [rcx + rdx], 'zero'
    je .boundary
    cmp rax, 5
    jb .no
    mov r10d, 5
    mov r11d, 1
    cmp dword [rcx + rdx], 'asci'
    jne .utf16
    cmp byte [rcx + rdx + 4], 'i'
    je .boundary
.utf16:
    cmp rax, 6
    jb .no
    cmp dword [rcx + rdx], 'utf1'
    jne .no
    cmp word [rcx + rdx + 4], '6z'
    jne .no
    mov r10d, 6
    mov r11d, 2
.boundary:
    cmp rax, r10
    je .recognized
    mov r9, rdx
    add r9, r10
    mov al, [rcx + r9]
    cmp al, '_'
    je .no
    cmp al, '0'
    jb .letter
    cmp al, '9'
    jbe .no
.letter:
    or al, 0x20
    cmp al, 'a'
    jb .recognized
    cmp al, 'z'
    jbe .no
.recognized:
    cmp qword [rbp + F_SEM_STAGE], IN_TEXT
    je .text_forbidden
    push rbx
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    sub rsp, 0x40
    mov rsi, rcx
    mov r13, r11                  ; 1=ascii, 2=utf16z, 3=zero.
    mov [rsp + 0x20], r8
    mov rax, [rbp + F_WORK]
    mov qword [rax + 0x20], 0
    mov qword [rax + 0x28], 0
    call b18_lex_header
    test rax, rax
    jnz .lex_error
    mov rbx, rdx
    mov rdi, [rbp + F_WORK]
    add rdi, 0x1C400
    cmp rbx, 2
    jb .missing
    mov rax, [rdi]
    add rax, [rdi + 8]
    cmp [rdi + 16], rax
    jbe .argument_error
    mov r15, [rbp + F_WORK]
    cmp qword [rbp + F_SEM_STAGE], IN_RDATA
    je .rdata
    cmp qword [rbp + F_SEM_STAGE], IN_DATA
    jne .internal
    add r15, 16
    mov qword [rsp + 0x30], 0x2000
    jmp .argument
.rdata:
    add r15, 8
    mov qword [rsp + 0x30], 0x4000
.argument:
    mov r12, [rdi + 16]
    cmp r13d, 3
    je .zero
%ifdef PRODUCE_PE
    mov rax, [rbp + F_WORK]
    cmp [rax + 0x178], r12
    jne .valid_string_token
    mov rdx, [rax + 0x170]
    test rdx, rdx
    jz .valid_string_token
    mov eax, 102                ; Ne pas accuser artificiellement le début d'une chaîne invalide.
    jmp .out
.valid_string_token:
%endif
    cmp qword [rdi + 24], 2
    jb .argument_error
    cmp byte [rsi + r12], 0x22
    jne .argument_error
    mov rax, r12
    add rax, [rdi + 24]
    dec rax
    cmp byte [rsi + rax], 0x22
    jne .argument_error
    mov [rsp + 0x28], rax         ; Index du guillemet final, jamais décodé.
    inc r12
    xor r14d, r14d
.decode:
    cmp r12, [rsp + 0x28]
    jae .string_done
    mov [rsp + 0x38], r12         ; Provenance du caractère décodé ou de son échappement.
    movzx eax, byte [rsi + r12]
    cmp al, 0x5C
    jne .emit
    inc r12
    movzx eax, byte [rsi + r12]
    cmp al, 0x5C
    je .emit
    cmp al, 0x22
    je .emit
    cmp al, 'n'
    je .newline
    cmp al, 'r'
    je .carriage
    cmp al, 't'
    je .tab
    cmp al, '0'
    je .nul
    cmp al, 'x'
    jne .decode_error
    mov rax, r12
    add rax, 2
    cmp rax, [rsp + 0x28]
    jae .decode_error
    inc r12
    movzx eax, byte [rsi + r12]
    call hex_digit_value          ; Ne modifie que RAX; EDX peut conserver le nibble haut.
    test eax, eax
    js .decode_error
    mov edx, eax
    shl edx, 4
    inc r12
    movzx eax, byte [rsi + r12]
    call hex_digit_value
    test eax, eax
    js .decode_error
    or eax, edx
    jmp .emit
.newline:
    mov eax, 10
    jmp .emit
.carriage:
    mov eax, 13
    jmp .emit
.tab:
    mov eax, 9
    jmp .emit
.nul:
    xor eax, eax
.emit:
    cmp r13d, 2
    jne .ascii_byte
    cmp eax, 0x7F
    ja .utf16_range
    cmp r14, 0x3FE
    ja .string_limit
    mov r11, [rbp + F_WORK]
    add r11, 0x1C500
    mov [r11 + r14], al
    mov byte [r11 + r14 + 1], 0
    add r14, 2
    jmp .next_char
.ascii_byte:
    cmp r14, 0x400
    jae .string_limit
    mov r11, [rbp + F_WORK]
    add r11, 0x1C500
    mov [r11 + r14], al
    inc r14
.next_char:
    inc r12
    jmp .decode
.string_done:
    cmp r13d, 2
    jne .publish
    cmp r14, 0x3FE
    ja .string_limit
    mov r11, [rbp + F_WORK]
    add r11, 0x1C500
    mov word [r11 + r14], 0       ; Terminaison UTF-16LE explicite.
    add r14, 2
    jmp .publish
.zero:
    mov al, [rsi + r12]
    cmp al, '0'
    jb .argument_error
    cmp al, '9'
    ja .argument_error
    lea rcx, [rsi + r12]
    mov rdx, [rdi + 24]
    mov r8, 0xFFFFFFFFFFFFFFFF
    call parse_unsigned_literal
    test r10d, r10d
    jnz .integer_error
    test rax, rax
    jz .argument_error
    mov r14, rax
.publish:
    cmp rbx, 2                    ; Vérifier l'argument avant d'accuser un token ultérieur.
    ja .extra
    mov rdx, [r15]
    add rdx, r14
    jc .capacity
    cmp rdx, [rsp + 0x30]
    ja .capacity
    mov rax, [rbp + F_WORK]
    cmp r13d, 3
    jne .publish_bytes
    mov rcx, r14
    cmp rcx, 0x400
    jbe .zero_count
    mov ecx, 0x400                ; Seul le préfixe tient en scratch; la description garde N.
.zero_count:
    lea r11, [rax + 0x1C500]
.zero_fill:
    mov byte [r11], 0
    inc r11
    dec rcx
    jnz .zero_fill
    mov qword [rax + 0x28], 2     ; ZERO décrit tous les N octets, même au-delà du scratch.
    jmp .commit
.publish_bytes:
    mov qword [rax + 0x28], 1
.commit:
    mov [r15], rdx
    mov [rax + 0x20], r14
    mov eax, 1
    jmp .out
.argument_error:
    mov rdx, [rdi + 16]
    inc rdx
    mov eax, 201
    jmp .out
.extra:
    mov rdx, [rdi + 32]
    inc rdx
    mov eax, 201
    jmp .out
.missing:
    mov rdx, [rsp + 0x20]
    inc rdx
    mov eax, 201
    jmp .out
.integer_error:
    lea rdx, [r12 + r11 + 1]
    mov eax, 102
    jmp .out
.utf16_range:
    mov rdx, [rsp + 0x38]
    inc rdx
    mov eax, 201
    jmp .out
.decode_error:
    lea rdx, [r12 + 1]
    mov eax, 102
    jmp .out
.string_limit:
    lea rdx, [r12 + 1]
    mov eax, 101
    jmp .out
.capacity:
    mov rdx, [rdi]
    inc rdx
    mov eax, 500
    jmp .out
.lex_error:
    xchg rax, rdx
    jmp .out
.internal:
    mov eax, 603
    xor edx, edx
.out:
    add rsp, 0x40
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    ret
.text_forbidden:
    inc rdx
    mov eax, 202
    ret
.no:
    xor eax, eax
    ret

hex_digit_value:
    cmp eax, '0'
    jb .bad
    cmp eax, '9'
    jbe .digit
    or eax, 0x20
    cmp eax, 'a'
    jb .bad
    cmp eax, 'f'
    ja .bad
    sub eax, 'a'-10
    ret
.digit:
    sub eax, '0'
    ret
.bad:
    mov eax, -1
    ret

%ifdef ADDR_SNAPSHOT
verify_address_snapshot:
    mov rcx, [rbp + F_WORK]
    cmp qword [rcx + 0x60], 3
    jne .bad
    cmp qword [rcx + 8], 24
    jne .bad
    cmp qword [rcx + 16], 8
    jne .bad
    cmp qword [rbp + F_TEXT_OFFSET], 1
    jne .bad
    add rcx, 0x12400
    lea rdx, [rel address_expected]
    xor r8d, r8d
.byte:
    mov al, [rcx + r8]
    cmp al, [rdx + r8]
    jne .bad
    inc r8
    cmp r8, address_expected_end - address_expected
    jb .byte
    xor eax, eax
    ret
.bad:
    mov eax, 1
    ret
%endif

%ifdef FIXUP_SNAPSHOT
verify_fixup_snapshot:
    cmp qword [rbp + F_TEXT_OFFSET], fixup_expected_end - fixup_expected
    jne .bad
    mov rcx, [rbp + F_WORK]
%if FIXUP_SNAPSHOT = 2
    cmp qword [rcx + 0x30], 11
    jne .bad
    cmp qword [rcx + 0x40], 1
    jne .bad
    cmp qword [rcx + 0x60], 1
    jne .bad
    cmp qword [rcx + 8], 50
    jne .bad
    cmp qword [rcx + 16], 8
    jne .bad
%else
    cmp qword [rcx + 0x30], 24
    jne .bad
    cmp qword [rcx + 0x40], 18
    jne .bad
%endif
    mov r8, [rbp + F_OUT]
    lea r9, [rcx + 0x6400]
%if FIXUP_SNAPSHOT = 2
    mov r10d, 1
%else
    mov r10d, 18
%endif
.patch:
    mov edx, [r9 + 4]
    cmp rdx, fixup_expected_end - fixup_expected - 4
    ja .bad
    mov eax, [r9 + 20]
    mov [r8 + rdx], eax
    add r9, 24
    dec r10
    jnz .patch
    lea r9, [rel fixup_expected]
    xor edx, edx
.byte:
    mov al, [r8 + rdx]
    cmp al, [r9 + rdx]
    jne .bad
    inc rdx
    cmp rdx, fixup_expected_end - fixup_expected
    jb .byte
    xor eax, eax
    ret
.bad:
    mov eax, 1
    ret
%endif

%ifdef INSN_SNAPSHOT
verify_instruction_snapshot:
    cmp qword [rbp + F_TEXT_OFFSET], instruction_expected_end - instruction_expected
    jne .bad
    mov rcx, [rbp + F_WORK]
%if INSN_SNAPSHOT = 2
    cmp qword [rcx + 0x30], 36
%else
    cmp qword [rcx + 0x30], 46
%endif
    jne .bad
    cmp qword [rbp + F_SYMBOL_COUNT], 2
    jne .bad
    ; K est le premier symbole, start le second : label TEXT à l'offset zéro.
    cmp word [rcx + 0x400 + 48 + 46], 0xA2
    jne .bad
    cmp qword [rcx + 0x400 + 48 + 32], 0
    jne .bad
    xor eax, eax
    ret
.bad:
    mov eax, 1
    ret
%endif

%ifdef TEXT_SNAPSHOT
verify_text_snapshot:
    mov rcx, [rbp + F_WORK]
    lea rdx, [rel text_expected]
    mov r8, [rdx]
    cmp [rcx + 0x20], r8
    jne .bad
    mov rax, [rdx + 8]
    cmp [rcx + 0x28], rax
    jne .bad
    mov r9, [rdx + 16]
    cmp [rcx + 8], r9
    jne .bad
    mov r9, [rdx + 24]
    cmp [rcx + 16], r9
    jne .bad
    add rcx, 0x1C500
    test r8, r8                  ; ascii vide : aucune lecture après une taille nulle.
    jz .ok
    cmp eax, 2
    je .zero
    add rdx, 32
.compare:
    mov al, [rcx]
    cmp al, [rdx]
    jne .bad
    inc rcx
    inc rdx
    dec r8
    jnz .compare
    jmp .ok
.zero:
    cmp r8, 0x400                ; Vérifier le préfixe matérialisé du remplissage implicite.
    jbe .zero_loop
    mov r8d, 0x400
.zero_loop:
    cmp byte [rcx], 0
    jne .bad
    inc rcx
    dec r8
    jnz .zero_loop
.ok:
    xor eax, eax
    ret
.bad:
    mov eax, 1
    ret
%endif

try_scalar_data:
    mov rax, r8
    sub rax, rdx
    cmp rax, 4
    jb .no
    mov r10d, 4
    mov r11d, 1
    cmp dword [rcx + rdx], 'byte'
    je .boundary
    mov r11d, 2
    cmp dword [rcx + rdx], 'word'
    je .boundary
    cmp rax, 5
    jb .no
    mov r10d, 5
    mov r11d, 4
    cmp dword [rcx + rdx], 'dwor'
    je .last_d
    mov r11d, 8
    cmp dword [rcx + rdx], 'qwor'
    jne .no
.last_d:
    cmp byte [rcx + rdx + 4], 'd'
    jne .no
.boundary:
    cmp rax, r10
    je .recognized
    add rax, 0                    ; RAX ne sert plus après ce contrôle de longueur.
    mov r9, rdx
    add r9, r10
    mov al, [rcx + r9]
    cmp al, '_'
    je .no
    cmp al, '0'
    jb .letter
    cmp al, '9'
    jbe .no
.letter:
    or al, 0x20
    cmp al, 'a'
    jb .recognized
    cmp al, 'z'
    jbe .no
.recognized:
    cmp qword [rbp + F_SEM_STAGE], IN_TEXT
    je .text_forbidden
    push rbx                      ; Les registres firmware sont non volatils : les restaurer.
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    sub rsp, 0x40                 ; Shadow 32 + quatre temporaires privés, pas les slots entry.
    mov rsi, rcx
    mov r13, r11                  ; Largeur de chaque valeur en octets.
    mov [rsp + 0x30], r8          ; Fin logique de ligne pour les diagnostics EOF.
    mov rax, [rbp + F_WORK]
    mov qword [rax + 0x20], 0     ; Aucune taille de scratch publiée tant que la ligne échoue.
    mov qword [rax + 0x28], 0     ; NONE, jusqu'à publication d'une ligne valide.
    call b18_lex_header
    test rax, rax
    jnz .lex_error
    mov rbx, rdx                  ; Nombre de tokens de cette ligne.
    mov rdi, [rbp + F_WORK]
    add rdi, 0x1C400
    cmp rbx, 2
    jb .missing
    mov rax, [rdi]
    add rax, [rdi + 8]
    cmp [rdi + 16], rax           ; WS requis avant le premier scalaire.
    jbe .first_argument_bad
    mov r15, [rbp + F_WORK]
    mov rax, [rbp + F_SEM_STAGE]
    cmp eax, IN_RDATA
    je .rdata
    cmp eax, IN_DATA
    jne .internal
    add r15, 16
    mov qword [rsp + 0x38], 0x2000
    jmp .alignment
.rdata:
    add r15, 8
    mov qword [rsp + 0x38], 0x4000
.alignment:
    mov rax, r13
    dec rax
    test [r15], rax               ; Pas de correction implicite de l'alignement naturel.
    jnz .unaligned
    mov r12d, 1                   ; Token du premier scalaire.
    xor r14d, r14d                ; Nombre d'octets préparés dans le scratch.
.value:
    mov rax, r12
    shl rax, 4
    lea r9, [rdi + rax]
    mov rdx, [r9]
    mov [rsp + 0x20], rdx         ; Début du scalaire, signe compris.
    mov qword [rsp + 0x28], 0     ; Flag lexical de signe.
    mov al, [rsi + rdx]
    cmp al, '-'
    jne .classify
    mov qword [rsp + 0x28], 1
    inc r12
    cmp r12, rbx
    jae .minus_gap
    mov rax, r12
    shl rax, 4
    lea r9, [rdi + rax]
    mov rdx, [r9]
    mov rax, [rsp + 0x20]
    inc rax
    cmp rdx, rax                  ; Le moins doit être immédiatement suivi de la magnitude.
    jne .minus_gap
    mov al, [rsi + rdx]
.classify:
    ; addr(...) est une forme explicite; un identifiant addr nu reste une constante.
    cmp qword [r9 + 8], 4
    jne .ordinary_scalar
    cmp dword [rsi + rdx], 'addr'
    jne .ordinary_scalar
    lea rax, [r12 + 1]
    cmp rax, rbx
    jae .ordinary_scalar
    shl rax, 4
    cmp qword [rdi + rax + 8], 1
    jne .ordinary_scalar
    mov rcx, [rdi + rax]
    cmp byte [rsi + rcx], '('
    je .address_value
.ordinary_scalar:
    mov al, [rsi + rdx]
    cmp al, '0'
    jb .identifier
    cmp al, '9'
    jbe .literal
.identifier:
    cmp qword [rsp + 0x28], 0
    jne .bad_magnitude
    cmp al, '_'
    je .lookup
    or al, 0x20
    cmp al, 'a'
    jb .value_syntax
    cmp al, 'z'
    ja .value_syntax
.lookup:
    mov rcx, [rbp + F_WORK]
    add rcx, 0x400
    mov r10, [rbp + F_SYMBOL_COUNT]
    mov r8, [r9 + 8]
.lookup_next:
    test r10, r10
    jz .symbol_error
    mov rdx, [r9]
    xor r11d, r11d
.lookup_compare:
    cmp r11, r8
    jae .lookup_length
    mov al, [rsi + rdx]
    cmp al, [rcx + r11]
    jne .lookup_miss
    inc rdx
    inc r11
    jmp .lookup_compare
.lookup_length:
    cmp byte [rcx + r11], 0
    jne .lookup_miss
    movzx eax, word [rcx + 46]
    mov edx, eax
    and eax, 0x0F
    cmp eax, 1                    ; Un label n'est jamais une constante scalaire implicite.
    jne .symbol_error
    and edx, 0x10
    mov [rsp + 0x28], rdx
    mov rax, [rcx + 32]
    test edx, edx
    jz .range
    neg rax                       ; Reconstituer la magnitude de la constante négative.
    jmp .range
.lookup_miss:
    add rcx, 48
    dec r10
    jmp .lookup_next
.address_value:
    cmp qword [rsp + 0x28], 0
    jne .bad_magnitude
    cmp r13, 8
    jne .address_width
    add r12, 2                 ; Passer addr et '('; le token courant est le nom.
    cmp r12, rbx
    jae .missing
    mov rax, r12
    shl rax, 4
    mov rcx, [rdi + rax]
    add rcx, rsi
    mov rdx, [rdi + rax + 8]
    call validate_symbol_spelling
    test eax, eax
    jnz .address_name
    inc r12
    cmp r12, rbx
    jae .missing
    mov rax, r12
    shl rax, 4
    mov rdx, [rdi + rax]
    cmp qword [rdi + rax + 8], 1
    jne .separator_error
    cmp byte [rsi + rdx], ')'
    jne .separator_error
    sub rax, 16
    lea rcx, [rdi + rax]        ; Record token du nom immuable.
    mov rdx, [r15]
    add rdx, r14
    mov r9, [rbp + F_SEM_STAGE]
    sub r9, IN_TEXT - 1
    call record_data_reference
    test eax, eax
    jnz .out
    xor eax, eax               ; Placeholder de mesure, jamais présenté comme adresse finale.
    jmp .store
.address_width:
    mov eax, 400
    jmp .value_position
.address_name:
    mov rax, r12
    shl rax, 4
    mov rdx, [rdi + rax]
    inc rdx
    mov eax, 400
    jmp .out
.literal:
    lea rcx, [rsi + rdx]
    mov rdx, [r9 + 8]
    mov r8, 0xFFFFFFFFFFFFFFFF
    cmp qword [rsp + 0x28], 0
    je .parse_literal
    mov r8, 0x8000000000000000
.parse_literal:
    call parse_unsigned_literal
    test r10d, r10d
    jnz .literal_error
.range:
    mov ecx, r13d
    shl ecx, 3                    ; Largeur en bits.
    cmp qword [rsp + 0x28], 0
    jne .negative_range
    cmp ecx, 64
    je .store                     ; Tous les u64 sont représentables dans qword.
    mov edx, 1
    shl rdx, cl
    dec rdx
    cmp rax, rdx
    ja .range_error
    jmp .store
.negative_range:
    dec ecx
    mov edx, 1
    shl rdx, cl
    cmp rax, rdx
    ja .range_error
    neg rax                       ; Bits de complément à deux, -0 inclus.
.store:
    mov rcx, r14
    add rcx, r13
    cmp rcx, 0x400                ; Capacité du scratch, indépendante du quota de tokens.
    ja .internal
    mov r11, [rbp + F_WORK]
    add r11, 0x1C500
    add r11, r14
    mov rcx, r13
.store_byte:
    mov [r11], al                 ; Little-endian explicite.
    shr rax, 8
    inc r11
    dec rcx
    jnz .store_byte
    add r14, r13
    inc r12
    cmp r12, rbx
    jae .commit
    mov rax, r12
    shl rax, 4
    lea r9, [rdi + rax]
    mov rdx, [r9]
    cmp qword [r9 + 8], 1
    jne .separator_error
    cmp byte [rsi + rdx], ','
    jne .separator_error
    inc r12
    cmp r12, rbx
    jae .missing
    jmp .value
.commit:
    mov rax, [r15]
    add rax, r14
    jc .capacity
    cmp rax, [rsp + 0x38]
    ja .capacity
    mov [r15], rax                ; Publication atomique du compteur de cette ligne.
    mov rax, [rbp + F_WORK]
    mov qword [rax + 0x28], 1     ; BYTES : la pass 2 utilisera le scratch exact.
    mov [rax + 0x20], r14         ; Le scratch devient utilisable après validation complète.
    mov eax, 1
    jmp .out
.literal_error:
    mov rax, r12
    shl rax, 4
    mov rdx, [rdi + rax]
    add rdx, r11
    inc rdx
    mov eax, 102
    jmp .out
.minus_gap:
    mov rdx, [rsp + 0x20]
    add rdx, 2                    ; Colonne de l'octet suivant le signe.
    mov eax, 102
    jmp .out
.bad_magnitude:
    inc rdx
    mov eax, 102
    jmp .out
.first_argument_bad:
    mov rdx, [rdi + 16]
    inc rdx
    mov eax, 201
    jmp .out
.separator_error:
    inc rdx
    mov eax, 201
    jmp .out
.value_syntax:
    mov eax, 201
    jmp .value_position
.symbol_error:
    mov eax, 301
    jmp .value_position
.range_error:
    mov eax, 401
.value_position:
    mov rdx, [rsp + 0x20]
    inc rdx
    jmp .out
.unaligned:
    mov eax, 402
    jmp .keyword_position
.capacity:
    mov eax, 500
.keyword_position:
    mov rdx, [rdi]
    inc rdx
    jmp .out
.missing:
    mov rdx, [rsp + 0x30]
    inc rdx
    mov eax, 201
    jmp .out
.lex_error:
    xchg rax, rdx
    jmp .out
.internal:
    mov eax, 603
    xor edx, edx
.out:
    add rsp, 0x40
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    ret
.text_forbidden:
    inc rdx
    mov eax, 202
    ret
.no:
    xor eax, eax
    ret

; RCX=octets du littéral, RDX=longueur, R8=magnitude maximale.
; Retour RAX=magnitude, R10=0/102, R11=index relatif de l'erreur.
parse_unsigned_literal:
    push rbx
    push rsi
    push rdi
    mov rsi, rcx
    mov rdi, rdx
    xor r11d, r11d
    test rdi, rdi
    jz .bad
    cmp byte [rsi], '0'
    jne .decimal
    cmp rdi, 1
    je .zero
    mov r11d, 1
    cmp byte [rsi + 1], 'x'
    jne .bad
    mov r11d, 2
    cmp rdi, 2
    je .bad
    mov r9, r8
    shr r9, 4
    mov rbx, r8
    and ebx, 15
    xor eax, eax
.hex:
    movzx edx, byte [rsi + r11]
    cmp edx, '0'
    jb .bad
    cmp edx, '9'
    jbe .hex_digit
    or edx, 0x20
    cmp edx, 'a'
    jb .bad
    cmp edx, 'f'
    ja .bad
    sub edx, 'a'-10
    jmp .hex_accumulate
.hex_digit:
    sub edx, '0'
.hex_accumulate:
    cmp rax, r9
    ja .bad
    jb .hex_fits
    cmp edx, ebx
    ja .bad
.hex_fits:
    shl rax, 4
    or rax, rdx
    inc r11
    cmp r11, rdi
    jb .hex
    jmp .ok
.decimal:
    mov rax, r8
    xor edx, edx
    mov ecx, 10
    div rcx
    mov r9, rax                   ; Seuil quotient et reste calculés une fois.
    mov rbx, rdx
    xor eax, eax
.decimal_loop:
    movzx edx, byte [rsi + r11]
    sub edx, '0'
    cmp edx, 9
    ja .bad
    cmp rax, r9
    ja .bad
    jb .decimal_fits
    cmp edx, ebx
    ja .bad
.decimal_fits:
    imul rax, rax, 10            ; Les comparaisons garantissent la plage u64.
    add rax, rdx
    inc r11
    cmp r11, rdi
    jb .decimal_loop
    jmp .ok
.zero:
    xor eax, eax
.ok:
    xor r10d, r10d
    jmp .out
.bad:
    xor eax, eax
    mov r10d, 102
.out:
    pop rdi
    pop rsi
    pop rbx
    ret

%ifdef SCALAR_SNAPSHOT
verify_scalar_snapshot:
    mov rcx, [rbp + F_WORK]
    lea rdx, [rel scalar_expected]
    mov r8, [rdx]                ; Nombre d'octets publiés.
    cmp [rcx + 0x20], r8
    jne .bad
    mov rax, [rdx + 8]          ; Compteur rdata attendu.
    cmp [rcx + 8], rax
    jne .bad
    mov rax, [rdx + 16]         ; Compteur data attendu.
    cmp [rcx + 16], rax
    jne .bad
    %if SCALAR_SNAPSHOT = 4
        cmp qword [rbp + F_SYMBOL_COUNT], 2
        jne .bad
        cmp qword [rcx + 0x420], 8
        jne .bad
        cmp qword [rcx + 0x450], 16
        jne .bad
    %endif
    add rcx, 0x1C500
    add rdx, 24
.compare:
    mov al, [rcx]
    cmp al, [rdx]
    jne .bad
    inc rcx
    inc rdx
    dec r8
    jnz .compare
    xor eax, eax
    ret
.bad:
    mov eax, 1
    ret
%endif

b112_try_align:
    mov rax, r8
    sub rax, rdx
    cmp rax, 5
    jb .no
    cmp dword [rcx + rdx], 'alig'
    jne .no
    cmp byte [rcx + rdx + 4], 'n'
    jne .no
    cmp rax, 5
    je .recognized
    mov al, [rcx + rdx + 5]
    cmp al, '_'
    je .no
    cmp al, '0'
    jb .letter
    cmp al, '9'
    jbe .no
.letter:
    or al, 0x20
    cmp al, 'a'
    jb .recognized
    cmp al, 'z'
    jbe .no
.recognized:
    push rbx
    push rsi
    push rdi
    sub rsp, 0x20                 ; Shadow space et alignement avant appels privés.
    mov rsi, rcx
    mov rdi, r8                   ; Fin logique pour diagnostiquer un argument absent.
    call b18_lex_header
    test rax, rax
    jnz .lex_error
    cmp edx, 2
    jb .missing
    mov rdi, rdx                  ; Compteur de tokens, conservé pendant la validation.
    mov rbx, [rbp + F_WORK]
    add rbx, 0x1C400
    mov rax, [rbx]
    add rax, 5
    cmp [rbx + 16], rax           ; WS obligatoire après align.
    jbe .argument_error
    mov r9, [rbx + 16]
    cmp qword [rbx + 24], 1
    je .one_digit
    cmp qword [rbx + 24], 2
    jne .argument_error
    cmp word [rsi + r9], '16'     ; La forme à deux chiffres autorisée.
    jne .argument_error
    mov edx, 16
    jmp .argument_valid
.one_digit:
    movzx edx, byte [rsi + r9]
    sub edx, '0'
    cmp edx, 2
    je .argument_valid
    cmp edx, 4
    je .argument_valid
    cmp edx, 8
    jne .argument_error
.argument_valid:
    cmp rdi, 2
    ja .extra
    mov rax, [rbp + F_SEM_STAGE]
    cmp eax, IN_TEXT
    je .text
    mov rdi, [rbp + F_WORK]
    cmp eax, IN_RDATA
    je .rdata
    cmp eax, IN_DATA
    jne .internal
    add rdi, 16                   ; Compteur du payload data.
    mov r8d, 0x2000
    jmp .compute
.rdata:
    add rdi, 8
    mov r8d, 0x4000
    jmp .compute
.text:
    lea rdi, [rbp + F_TEXT_OFFSET]
    mov r8d, 0x8000
.compute:
    mov rcx, [rdi]                ; Offset actuel, n et maximum : aucune mutation préalable.
    call b112_align_value
    test edx, edx
    jnz .value_error
%ifdef PRODUCE_PE
    mov r10, [rbp + F_WORK]
    mov r11, rax
    sub r11, [rdi]
    mov [r10 + 0x20], r11
    mov qword [r10 + 0x28], 2
    cmp qword [rbp + F_SEM_STAGE], IN_TEXT
    jne .fill_ready
    mov qword [r10 + 0x28], 3
.fill_ready:
%endif
    mov [rdi], rax                ; Publier seulement une mesure entièrement validée.
    mov eax, 1
    jmp .out
.value_error:
    mov eax, edx
    jmp .argument_position
.argument_error:
    mov eax, 201
.argument_position:
    mov rdx, [rbx + 16]
    inc rdx
    jmp .out
.extra:
    mov rdx, [rbx + 32]
    inc rdx
    mov eax, 201
    jmp .out
.missing:
    lea rdx, [rdi + 1]
    mov eax, 201
    jmp .out
.lex_error:
    xchg rax, rdx
    jmp .out
.internal:
    mov eax, 603
    xor edx, edx
.out:
    add rsp, 0x20
    pop rdi
    pop rsi
    pop rbx
    ret
.no:
    xor eax, eax
    ret

; Fonction pure : rcx=offset, rdx=alignement, r8=maximum.
; Retour rax=nouvel offset, edx=0; en erreur rax=0 et edx=201/500.
b112_align_value:
    cmp rdx, 2
    jb .invalid
    cmp rdx, 16
    ja .invalid
    lea r9, [rdx - 1]
    test rdx, r9                  ; Une puissance de deux possède un seul bit.
    jnz .invalid
    mov rax, rcx
    add rax, r9                   ; Vérifier la retenue avant l'arrondi.
    jc .overflow
    not r9
    and rax, r9
    cmp rax, r8
    ja .overflow
    xor edx, edx
    ret
.invalid:
    xor eax, eax
    mov edx, 201
    ret
.overflow:
    xor eax, eax
    mov edx, 500
    ret

b111_try_label:
    mov r9, rdx                   ; Examiner le premier groupe sans toucher aux arguments.
.probe:
    cmp r9, r8
    jae .not_label
    mov al, [rcx + r9]
    cmp al, ':'
    je .recognized
    cmp al, 0x20
    je .probe_ws
    cmp al, 0x09
    je .probe_ws
    inc r9
    jmp .probe
.probe_ws:
    inc r9
    cmp r9, r8
    jae .not_label
    mov al, [rcx + r9]
    cmp al, 0x20
    je .probe_ws
    cmp al, 0x09
    je .probe_ws
    cmp al, ':'
    je .recognized
.not_label:
    xor eax, eax
    ret
.recognized:
    push rbx                      ; Sauvegarder les non-volatils employés par le parseur.
    push rsi
    push rdi
    sub rsp, 0x20                 ; Alignement 16 et shadow space avant l'appel feuille.
    mov rsi, rcx                  ; Conserver la ligne physique.
    call b18_lex_header            ; Même table bornée et mêmes limites lexicales.
    test rax, rax
    jnz .lex_error
    mov ebx, edx                  ; Nombre de tokens de cette ligne.
    mov rdi, [rbp + F_WORK]
    add rdi, 0x1C400              ; Deux qwords par tranche de token.
    mov r11, [rdi]
    mov al, [rsi + r11]           ; Un label commence par lettre ou tiret bas.
    cmp al, '_'
    je .valid_start
    or al, 0x20
    cmp al, 'a'
    jb .name_bad
    cmp al, 'z'
    ja .name_bad
.valid_start:
    cmp ebx, 2
    jb .name_bad
    cmp qword [rdi + 24], 1       ; La ponctuation ':' est un seul octet.
    jne .colon_bad
    mov r11, [rdi + 16]
    cmp byte [rsi + r11], ':'
    jne .colon_bad
    cmp ebx, 2
    ja .extra
    mov rbx, [rbp + F_SYMBOL_COUNT] ; Constantes et labels occupent la même table.
    cmp rbx, 512
    ja .internal
    mov r9, [rbp + F_WORK]
    add r9, 0x400                 ; Premier record de symbole.
    xor r10d, r10d                ; Index du record à comparer.
.duplicate_next:
    cmp r10, rbx
    jae .insert
    mov r11, [rdi]                ; Début du nom dans la ligne.
    mov r8, [rdi + 8]             ; Longueur bornée à 31.
    xor edx, edx
.duplicate_compare:
    cmp rdx, r8
    jae .duplicate_length
    mov al, [rsi + r11]
    cmp al, [r9 + rdx]            ; Comparaison exacte et sensible à la casse.
    jne .duplicate_miss
    inc r11
    inc rdx
    jmp .duplicate_compare
.duplicate_length:
    cmp byte [r9 + rdx], 0        ; Un préfixe ne constitue pas un doublon.
    je .duplicate
.duplicate_miss:
    add r9, 48
    inc r10
    jmp .duplicate_next
.insert:
    cmp r10, 512                  ; Après la recherche, refuser toute écriture au-delà.
    jae .capacity
%ifdef PRODUCE_PE
    SAVE_PASS_RECORD r9, 48
%endif
    mov qword [r9], 0             ; Initialiser le record, y compris terminaison et flags.
    mov qword [r9 + 8], 0
    mov qword [r9 + 16], 0
    mov qword [r9 + 24], 0
    mov qword [r9 + 32], 0
    mov qword [r9 + 40], 0
    mov r11, [rdi]
    mov r8, [rdi + 8]
    xor edx, edx
.copy:
    cmp rdx, r8
    jae .value
    mov al, [rsi + r11]
    mov [r9 + rdx], al
    inc r11
    inc rdx
    jmp .copy
.value:
    mov rax, [rbp + F_SEM_STAGE]
    cmp eax, IN_TEXT
    je .text_offset
    mov r11, [rbp + F_WORK]
    cmp eax, IN_RDATA
    je .rdata_offset
    cmp eax, IN_DATA
    jne .internal
    mov rdx, [r11 + 16]
    jmp .store_value
.rdata_offset:
    mov rdx, [r11 + 8]
    jmp .store_value
.text_offset:
    mov rdx, [rbp + F_TEXT_OFFSET]
.store_value:
    mov [r9 + 32], rdx            ; Le label ne modifie pas l'offset du payload.
    sub eax, 3                    ; États 4/5/6 -> sections 1/2/3.
    shl eax, 5
    or eax, 2                     ; Kind LABEL, bit début d'instruction encore nul.
    mov [r9 + 46], ax
    mov rax, [rbp + F_SEM_START]
    add rax, [rdi]
    mov [r9 + 40], eax            ; Source bornée à 65 535 octets.
    mov rax, [rbp + F_SEM_LINE]
    mov [r9 + 44], ax             ; Ligne bornée à 4 096.
%ifdef PRODUCE_PE
    CHECK_PASS_RECORD r9, 48, .inconsistent
%endif
    inc r10
    mov [rbp + F_SYMBOL_COUNT], r10
    mov rax, [rbp + F_WORK]
    mov [rax], r10d               ; Compteur observable de la table commune.
    mov eax, 1                    ; Succès d'insertion, continuer le scan des lignes.
    jmp .out
%ifdef PRODUCE_PE
.inconsistent:
    mov eax, 600
    jmp .name_position
%endif
.duplicate:
    mov eax, 300
    jmp .name_position
.capacity:
    mov eax, 500
    jmp .name_position
.name_bad:
    mov eax, 201
.name_position:
    mov rdx, [rdi]
    inc rdx
    jmp .out
.colon_bad:
    mov rdx, [rdi + 16]
    inc rdx
    mov eax, 201
    jmp .out
.extra:
    mov rdx, [rdi + 32]
    inc rdx
    mov eax, 201
    jmp .out
.lex_error:
    xchg rax, rdx                 ; Lexer: colonne/code -> retour label: code/colonne.
    jmp .out
.internal:
    mov eax, 603
    xor edx, edx
.out:
    add rsp, 0x20
    pop rdi
    pop rsi
    pop rbx
    ret

%ifdef PRODUCE_PE
; Préserver les tranches du lexer pendant l'enregistrement du diagnostic.
; La chaîne invalide reste un token borné, jamais une valeur autorisée à être émise.
remember_lexer_string_error:
    push rdi
    push rax
    push rcx
    push rdx
    push r8
    push r9
    push r10
    push r11
    sub rsp, 0x28
    mov rax, [rbp + F_WORK]
    cmp qword [rax + 0x170], 0
    jne .done
    lea r10, [rdx + 1]
    mov [rax + 0x170], r10
    mov [rax + 0x178], r11
    mov rcx, [rbp + F_SEM_START]
    add rcx, rdx
    mov rdx, r10
    mov rax, [rbp + F_SEM_LINE]
    mov edi, 102
    call remember_semantic_error
.done:
    add rsp, 0x28
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rcx
    pop rax
    pop rdi
    ret
%endif

b18_lex_header:
    mov r9, [rbp + F_WORK]         ; Pool WORK validé et encore possédé.
%ifdef PRODUCE_PE
    mov qword [r9 + 0x170], 0    ; Première erreur de chaîne de ce lexage, colonne locale.
    mov qword [r9 + 0x178], -1   ; Début du token concerné; jamais une adresse de sortie.
%endif
    add r9, 0x1C400               ; Scratch déjà réservé pour les 16 tranches.
    xor r10d, r10d                ; Aucun token émis initialement.
.next:
    cmp rdx, r8                   ; La fin exclusive n'est jamais déréférencée.
    jae .done                     ; Fin de ligne : livrer le compte.
    mov al, [rcx + rdx]           ; Lire l'octet courant borné.
    cmp al, 0x20                  ; Un espace ne compte pas comme token.
    je .space
    cmp al, 0x09                  ; Même traitement pour une tabulation.
    je .space
    cmp r10d, 16                  ; Vérifier avant toute écriture de record.
    jae .limit                    ; Le début du 17e token déclenche E101.
    inc r10d                      ; Réserver exactement un record.
    mov r11, rdx                  ; Mémoriser le début du token.
    cmp al, 0x22                  ; Une chaîne complète compte comme un seul token.
    je .string_start
    cmp al, '.'                   ; Le point initial appartient au token section.
    jne .word
    inc rdx                       ; Inclure le point puis scanner son suffixe.
    jmp .word
.string_start:
    inc rdx
.string_loop:
    cmp rdx, r8
    jae .bad_string
    mov al, [rcx + rdx]
    cmp al, 0x22
    je .string_closed
    cmp al, 0x20
    jb .bad_string
    cmp al, 0x5C
    jne .string_next
    inc rdx
    cmp rdx, r8
    jae .bad_string
    mov al, [rcx + rdx]
    cmp al, 0x5C
    je .string_next
    cmp al, 0x22
    je .string_next
    cmp al, 'r'
    je .string_next
    cmp al, 'n'
    je .string_next
    cmp al, 't'
    je .string_next
    cmp al, '0'
    je .string_next
    cmp al, 'x'
    jne .bad_string
    inc rdx
    cmp rdx, r8
    jae .bad_string
    mov al, [rcx + rdx]
    cmp al, '0'
    jb .bad_string
    cmp al, '9'
    jbe .hex_second
    or al, 0x20
    cmp al, 'a'
    jb .bad_string
    cmp al, 'f'
    ja .bad_string
.hex_second:
    inc rdx
    cmp rdx, r8
    jae .bad_string
    mov al, [rcx + rdx]
    cmp al, '0'
    jb .bad_string
    cmp al, '9'
    jbe .string_next
    or al, 0x20
    cmp al, 'a'
    jb .bad_string
    cmp al, 'f'
    ja .bad_string
.string_next:
    inc rdx
    jmp .string_loop
.string_closed:
    inc rdx
    jmp .record
.bad_string:
%ifdef PRODUCE_PE
    mov rax, [rbp + F_WORK]
    cmp qword [rax + 0x100], 1
    jne .string_error_return
    sub rsp, 0x28
    call remember_lexer_string_error
    add rsp, 0x28
    cmp rdx, r8
    jae .record                 ; Token non fermé : tranche bornée par la vraie fin de ligne.
    cmp byte [rcx + rdx], 0x22
    je .string_closed           ; Un mauvais hex peut rencontrer le guillemet final.
    inc rdx
    jmp .string_loop
.string_error_return:
%endif
    lea rax, [rdx + 1]
    mov edx, 102
    ret
.space:
    inc rdx                       ; Consommer WS sans incrémenter le nombre de tokens.
    jmp .next
.word:
    cmp rdx, r8                   ; Contrôler la borne avant chaque lecture.
    jae .record
    mov al, [rcx + rdx]           ; Examiner la suite alphanumérique du token.
    cmp al, '_'                   ; Le tiret bas fait partie d'un identifiant.
    je .word_char
    cmp al, '0'                   ; Tester d'abord la plage des chiffres.
    jb .letter
    cmp al, '9'
    jbe .word_char
.letter:
    or al, 0x20                   ; Classer A-Z/a-z sans modifier SRC ni sa casse.
    cmp al, 'a'
    jb .separator
    cmp al, 'z'
    ja .separator
.word_char:
    mov al, [rcx + r11]           ; Identifier la catégorie depuis le premier octet.
    cmp al, '.'                   ; Les noms de section seront comparés exactement.
    je .advance
    cmp al, '0'
    jb .ident_limit
    cmp al, '9'                   ; Les entiers seront validés par leurs futurs parseurs.
    jbe .advance
.ident_limit:
    mov rax, rdx                  ; Calculer la longueur déjà consommée.
    sub rax, r11
    cmp rax, 31                   ; Refuser le 32e octet avant de le consommer.
    jae .limit
.advance:
    inc rdx                       ; Avancer d'un octet du token courant.
    jmp .word
.separator:
    cmp rdx, r11                  ; Un caractère isolé compte comme ponctuation.
    jne .record                   ; Sinon le token précédent se termine avant lui.
    inc rdx                       ; Consommer un seul caractère isolé.
.record:
    mov [r9], r11                 ; Stocker uniquement un index dans SRC immuable.
    mov rax, rdx
    sub rax, r11                  ; Longueur positive du token.
    mov [r9 + 8], rax
    add r9, 16                    ; Prochain record, vérifié avant sa future écriture.
    jmp .next
.done:
    xor eax, eax                  ; Zéro indique un lexage réussi.
    mov edx, r10d                 ; Retourner le nombre de tranches.
    ret
.limit:
    lea rax, [rdx + 1]            ; Colonne du premier octet excédentaire.
    mov edx, 101                  ; E101 : limite lexicale locale.
    ret

%ifdef B111_SNAPSHOT
b111_verify_symbols:
    cmp qword [rbp + F_SYMBOL_COUNT], 3
    jne .bad
    mov rcx, [rbp + F_WORK]
    cmp dword [rcx], 3
    jne .bad
    add rcx, 0x400
    lea rdx, [rel b111_expected_symbols]
    mov r8b, 144                  ; 144 tient dans u8; seuls ces huit bits sont utilisés.
.compare:
    mov al, [rcx]
    cmp al, [rdx]
    jne .bad
    inc rcx
    inc rdx
    dec r8b
    jnz .compare
    xor eax, eax
    ret
.bad:
    mov eax, 1
    ret
%endif

%ifdef B18_SNAPSHOT
verify_b18_snapshot:
    lea rcx, [rbp + F_ENTRY_SEEN] ; Début des cinq qwords entry et du qword TEXT_OFFSET.
    lea rdx, [rel b18_state_expected] ; Oracle indépendant dérivé de la fixture C01.
    mov r8d, 48                   ; Six qwords de huit octets, aucune zone supplémentaire.
.compare:
    mov al, [rcx]                 ; Lire un octet d'état encore vivant sur la pile.
    cmp al, [rdx]                 ; Comparer au golden avant de déplacer les curseurs.
    jne .bad
    inc rcx
    inc rdx
    dec r8d
    jnz .compare
    xor eax, eax                  ; Tous les champs sont identiques.
    ret
.bad:
    mov eax, 1                    ; Le cleanup transformera ce résultat en E603.
    ret
%endif

; b18_token_equal(rcx=ligne, rdx=record, r8=mot, r9=longueur) -> eax=0/1.
; Les arguments et les registres non volatils sont conservés.
b18_token_equal:
    cmp [rdx + 8], r9             ; Vérifier la longueur avant les lectures.
    jne .different
    mov r10, [rdx]                ; Début relatif du token borné.
    xor r11d, r11d                ; Index dans le mot attendu.
.compare:
    mov al, [rcx + r10]           ; Lire un octet appartenant à la tranche.
    cmp al, [r8 + r11]            ; Comparaison exacte, sensible à la casse.
    jne .different
    inc r10
    inc r11
    cmp r11, r9                   ; La longueur du mot est toujours non nulle.
    jb .compare
    mov eax, 1                    ; Le mot et la tranche sont identiques.
    ret
.different:
    xor eax, eax                  ; Un écart de contenu ou taille suffit au rejet.
    ret

; b18_body_error(rcx=ligne, rdx=debut, r8=fin) -> rax=index+1, edx=code.
; Aucun opérande, label ou octet machine n'est consommé par ce classificateur.
; Détecter le mot section, sans confondre sectionnement avec une transition.
; RCX/RDX/R8 sont préservés; routine feuille, aucun registre non volatil modifié.
b19_is_section:
    mov rax, r8
    sub rax, rdx
    cmp rax, 7                    ; Éviter toute lecture hors de la ligne.
    jb .no
    cmp dword [rcx + rdx], 'sect'
    jne .no
    cmp word [rcx + rdx + 4], 'io'
    jne .no
    cmp byte [rcx + rdx + 6], 'n'
    jne .no
    cmp rax, 7
    je .yes
    mov al, [rcx + rdx + 7]       ; Le huitième octet doit terminer le mot.
    cmp al, '_'
    je .no
    cmp al, '0'
    jb .letter
    cmp al, '9'
    jbe .no
.letter:
    or al, 0x20
    cmp al, 'a'
    jb .yes
    cmp al, 'z'
    jbe .no
.yes:
    mov eax, 1
    ret
.no:
    xor eax, eax
    ret

b18_body_error:
    sub r8, rdx                   ; Longueur disponible à partir du premier token.
    add rcx, rdx                  ; Pointer le premier octet sémantique déjà borné.
    xor r10d, r10d                ; Longueur du premier identifiant.
.scan:
    cmp r10, r8                   ; Arrêter au plus tard à la fin logique.
    jae .find
    mov al, [rcx + r10]
    cmp al, '_'
    je .char
    cmp al, '0'
    jb .letter
    cmp al, '9'
    jbe .char
.letter:
    or al, 0x20                   ; Classer les lettres sans altérer le source.
    cmp al, 'a'
    jb .find
    cmp al, 'z'
    ja .find
.char:
    inc r10                       ; Le suffixe alphanumérique empêche les faux préfixes.
    jmp .scan
.find:
    ; R9 est la table de catégories choisie par la section appelante.
.entry:
    movzx eax, byte [r9]          ; Lire la longueur du mot candidat.
    test eax, eax
    jz .unsupported               ; Aucun mot réservé : E400 provisoire.
    cmp r10, rax                  ; bytecode ne doit jamais correspondre à byte.
    jne .next
    xor r11d, r11d                ; Comparer seulement ce mot de même longueur.
.compare:
    mov al, [r9 + r11 + 3]        ; Les trois premiers octets sont longueur et code.
    cmp al, [rcx + r11]           ; Préserver la distinction de casse.
    jne .next
    inc r11
    cmp r11, r10
    jb .compare
    movzx eax, word [r9 + 1]      ; Code E200 pour en-tête, E202 pour données.
    jmp .result
.next:
    movzx eax, byte [r9]          ; Relire la longueur, AL ayant servi à comparer.
    lea r9, [r9 + rax + 3]        ; Sauter le record variable complet.
    jmp .entry
.unsupported:
    mov eax, 400                  ; Les instructions et formes futures ne passent pas.
.result:
    mov r10d, eax                 ; Garder le code pendant la conversion de position.
    lea rax, [rdx + 1]            ; Le début local devient une colonne un-based.
    mov edx, r10d
    ret

; --- Helpers privés --------------------------------------------------------

%ifdef B15_SNAPSHOT_MULTI
verify_b15_multi_snapshot:
    mov rcx, [rbp + F_WORK]
    test rcx, rcx
    jz .bad
    cmp qword [rbp + F_SYMBOL_COUNT], 3
    jne .bad
    cmp dword [rcx], 3
    jne .bad
    add rcx, 0x400
    jc .bad
    lea rdx, [rel b15_multi_expected]
    mov r8d, 144
.compare:
    mov al, [rcx]
    cmp al, [rdx]
    jne .bad
    inc rcx
    inc rdx
    dec r8d
    jnz .compare
    xor eax, eax
    ret
.bad:
    mov eax, 1
    ret
%endif

%ifdef B16_SNAPSHOT_CASE
verify_b16_snapshot:
    mov rcx, [rbp + F_WORK]
    test rcx, rcx
    jz .bad
    %if B16_SNAPSHOT_CASE = 7
        mov r9d, 2
        mov r8d, 96
    %else
        mov r9d, 1
        mov r8d, 48
    %endif
    cmp qword [rbp + F_SYMBOL_COUNT], r9
    jne .bad
    cmp dword [rcx], r9d
    jne .bad
    add rcx, 0x400
    jc .bad
    lea rdx, [rel b16_snapshot_expected]
.compare:
    mov al, [rcx]
    cmp al, [rdx]
    jne .bad
    inc rcx
    inc rdx
    dec r8d
    jnz .compare
    xor eax, eax
    ret
.bad:
    mov eax, 1
    ret
%endif

%ifdef B17_SNAPSHOT_CASE
verify_b17_snapshot:
    lea rcx, [rbp + F_ENTRY_SEEN]
    lea rdx, [rel b17_snapshot_expected]
    mov r8d, 40
.compare:
    mov al, [rcx]
    cmp al, [rdx]
    jne .bad
    inc rcx
    inc rdx
    dec r8d
    jnz .compare
    xor eax, eax
    ret
.bad:
    mov eax, 1
    ret
%endif

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

; rcx=line, rdx=begin, r8=end. Retour eax: 0=autre, 1=const, 2=entry.
; Les registres d'entrée restent inchangés afin que le parseur choisi les réutilise.
classify_const_or_entry:
    mov rax, r8
    sub rax, rdx
    cmp rax, 5
    jb .other
    cmp byte [rcx + rdx], 'c'
    jne .try_entry
    cmp byte [rcx + rdx + 1], 'o'
    jne .try_entry
    cmp byte [rcx + rdx + 2], 'n'
    jne .try_entry
    cmp byte [rcx + rdx + 3], 's'
    jne .try_entry
    cmp byte [rcx + rdx + 4], 't'
    jne .try_entry
    mov r9d, 1
    jmp .boundary
.try_entry:
    cmp byte [rcx + rdx], 'e'
    jne .other
    cmp byte [rcx + rdx + 1], 'n'
    jne .other
    cmp byte [rcx + rdx + 2], 't'
    jne .other
    cmp byte [rcx + rdx + 3], 'r'
    jne .other
    cmp byte [rcx + rdx + 4], 'y'
    jne .other
    mov r9d, 2
.boundary:
    lea rax, [rdx + 5]
    cmp rax, r8
    jae .classified
    mov al, [rcx + rax]
    cmp al, '_'
    je .other
    cmp al, '0'
    jb .upper
    cmp al, '9'
    jbe .other
.upper:
    cmp al, 'A'
    jb .lower
    cmp al, 'Z'
    jbe .other
.lower:
    cmp al, 'a'
    jb .classified
    cmp al, 'z'
    jbe .other
.classified:
    mov eax, r9d
    ret
.other:
    xor eax, eax
    ret

; rcx=line, rdx=begin, r8=end. Retour 0/edx=1 ou index+1/edx=101|201.
match_entry_line:
    add rdx, 5
    cmp rdx, r8
    jae .e201
    mov al, [rcx + rdx]
    cmp al, 0x20
    je .skip_keyword_ws
    cmp al, 0x09
    jne .e201
.skip_keyword_ws:
    inc rdx
    cmp rdx, r8
    jae .e201
    mov al, [rcx + rdx]
    cmp al, 0x20
    je .skip_keyword_ws
    cmp al, 0x09
    je .skip_keyword_ws

    cmp al, '_'
    je .identifier_start
    cmp al, 'A'
    jb .first_lower
    cmp al, 'Z'
    jbe .identifier_start
.first_lower:
    cmp al, 'a'
    jb .e201
    cmp al, 'z'
    ja .e201

.identifier_start:
    mov r9, rdx
    xor r10d, r10d
.identifier_valid:
    cmp r10d, 31
    jae .e101
    inc r10d
    inc rdx
.identifier_loop:
    cmp rdx, r8
    jae .identifier_done
    mov al, [rcx + rdx]
    cmp al, '_'
    je .identifier_valid
    cmp al, '0'
    jb .identifier_upper
    cmp al, '9'
    jbe .identifier_valid
.identifier_upper:
    cmp al, 'A'
    jb .identifier_lower
    cmp al, 'Z'
    jbe .identifier_valid
.identifier_lower:
    cmp al, 'a'
    jb .identifier_done
    cmp al, 'z'
    jbe .identifier_valid

.identifier_done:
    cmp r10d, 5
    jne .scan_extras
    cmp byte [rcx + r9], 'c'
    jne .reserved_entry
    cmp byte [rcx + r9 + 1], 'o'
    jne .reserved_entry
    cmp byte [rcx + r9 + 2], 'n'
    jne .reserved_entry
    cmp byte [rcx + r9 + 3], 's'
    jne .reserved_entry
    cmp byte [rcx + r9 + 4], 't'
    je .reserved
.reserved_entry:
    cmp byte [rcx + r9], 'e'
    jne .scan_extras
    cmp byte [rcx + r9 + 1], 'n'
    jne .scan_extras
    cmp byte [rcx + r9 + 2], 't'
    jne .scan_extras
    cmp byte [rcx + r9 + 3], 'r'
    jne .scan_extras
    cmp byte [rcx + r9 + 4], 'y'
    jne .scan_extras
.reserved:
    mov rdx, r9
    jmp .e201

.scan_extras:
    mov qword [rbp + F_CONST_META], -1
    mov r11d, 2
.token_skip_ws:
    cmp rdx, r8
    jae .tokens_done
    mov al, [rcx + rdx]
    cmp al, 0x20
    je .token_skip_one
    cmp al, 0x09
    jne .token_begin
.token_skip_one:
    inc rdx
    jmp .token_skip_ws
.token_begin:
    inc r11d
    cmp r11d, 16
    ja .e101
    cmp r11d, 3
    jne .token_classify
    mov [rbp + F_CONST_META], rdx
.token_classify:
    mov al, [rcx + rdx]
    cmp al, '('
    jb .token_check_colon
    cmp al, '-'
    jbe .token_single
.token_check_colon:
    cmp al, ':'
    je .token_single
    cmp al, '='
    je .token_single
    cmp al, '['
    je .token_single
    cmp al, ']'
    je .token_single
    cmp al, '0'
    jb .token_check_ident
    cmp al, '9'
    jbe .token_numeric_run
.token_check_ident:
    cmp al, '_'
    je .token_ident_run
    cmp al, 'A'
    jb .token_single
    cmp al, 'Z'
    jbe .token_ident_run
    cmp al, 'a'
    jb .token_single
    cmp al, 'z'
    jbe .token_ident_run
.token_single:
    inc rdx
    jmp .token_skip_ws
.token_numeric_run:
    inc rdx
    cmp rdx, r8
    jae .tokens_done
    mov al, [rcx + rdx]
    cmp al, 0x20
    je .token_skip_ws
    cmp al, 0x09
    je .token_skip_ws
    cmp al, '('
    jb .numeric_colon
    cmp al, '-'
    jbe .token_begin
.numeric_colon:
    cmp al, ':'
    je .token_begin
    cmp al, '='
    je .token_begin
    cmp al, '['
    je .token_begin
    cmp al, ']'
    je .token_begin
    jmp .token_numeric_run
.token_ident_run:
    inc rdx
    cmp rdx, r8
    jae .tokens_done
    mov al, [rcx + rdx]
    cmp al, 0x20
    je .token_skip_ws
    cmp al, 0x09
    je .token_skip_ws
    cmp al, '_'
    je .token_ident_run
    cmp al, '0'
    jb .ident_upper
    cmp al, '9'
    jbe .token_ident_run
.ident_upper:
    cmp al, 'A'
    jb .ident_lower
    cmp al, 'Z'
    jbe .token_ident_run
.ident_lower:
    cmp al, 'a'
    jb .token_begin
    cmp al, 'z'
    jbe .token_ident_run
    jmp .token_begin

.tokens_done:
    mov rdx, [rbp + F_CONST_META]
    cmp rdx, -1
    jne .e201
    mov rax, [rbp + F_SEM_START]
    add rax, r9
    jc fail_e603
    mov [rbp + F_ENTRY_NAME_OFF], rax
    mov [rbp + F_ENTRY_NAME_LEN], r10
    mov rax, [rbp + F_SEM_LINE]
    mov [rbp + F_ENTRY_LINE], rax
    mov rax, [rbp + F_SEM_START]
    mov [rbp + F_ENTRY_LINE_START], rax
    mov qword [rbp + F_ENTRY_SEEN], 1
    xor eax, eax
    mov edx, 1
    ret
.e101:
    lea rax, [rdx + 1]
    mov edx, 101
    ret
.e201:
    lea rax, [rdx + 1]
    mov edx, 201
    ret

validate_entry_descriptor:
    cmp qword [rbp + F_SEM_STAGE], IN_DATA ; Le succès exige les trois en-têtes.
    jne entry_descriptor_bad
validate_entry_fields:
    cmp qword [rbp + F_ENTRY_SEEN], 1
    jne entry_descriptor_bad
    mov rcx, [rbp + F_ENTRY_NAME_LEN]
    test rcx, rcx
    jz entry_descriptor_bad
    cmp rcx, 31
    ja entry_descriptor_bad
    mov rax, [rbp + F_ENTRY_NAME_OFF]
    cmp rax, [rbp + F_SOURCE_LEN]
    jae entry_descriptor_bad
    mov rdx, rax
    add rdx, rcx
    jc entry_descriptor_bad
    cmp rdx, [rbp + F_SOURCE_LEN]
    ja entry_descriptor_bad
    cmp rax, [rbp + F_ENTRY_LINE_START]
    jb entry_descriptor_bad
    mov rax, [rbp + F_ENTRY_LINE]
    test rax, rax
    jz entry_descriptor_bad
    cmp rax, [rbp + F_LINES_DONE]
    ja entry_descriptor_bad
    xor eax, eax
    ret
entry_descriptor_bad:
    mov eax, 1
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
    ; `const` est le mot-clé réservé canonique refusé comme identifiant.
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
    mov dword [rbp + F_CONST_NAME_PACK], r9d
    mov dword [rbp + F_CONST_NAME_PACK + 4], r10d
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
    inc rdx

    ; Lexage borné de la ligne complète : 3 tokens préfixe + tokens de valeur.
    ; La ponctuation NTASM est toujours un token autonome, même accolée.
    mov qword [rbp + F_TOKEN_START], -1
    mov qword [rbp + F_TOKEN_END], -1
    mov qword [rbp + F_EXTRA_START], -1
    mov qword [rbp + F_AFTER_START], -1
    mov qword [rbp + F_CONST_META], 0x0001
    mov r11d, 3
.token_skip_ws:
    cmp rdx, r8
    jae .tokens_done
    mov al, [rcx + rdx]
    cmp al, 0x20
    je .token_skip_one
    cmp al, 0x09
    jne .token_begin
.token_skip_one:
    inc rdx
    jmp .token_skip_ws
.token_begin:
    inc r11d
    cmp r11d, 16
    ja .e101
    cmp r11d, 4
    jne .maybe_extra_token
    mov [rbp + F_TOKEN_START], rdx
    jmp .token_classify
.maybe_extra_token:
    cmp r11d, 5
    jne .maybe_after_value
    mov [rbp + F_EXTRA_START], rdx
    jmp .token_classify
.maybe_after_value:
    cmp r11d, 6
    jne .token_classify
    mov rax, [rbp + F_TOKEN_START]
    cmp byte [rcx + rax], '-'
    jne .token_classify
    mov [rbp + F_AFTER_START], rdx
.token_classify:
    mov al, [rcx + rdx]
    cmp al, '('
    jb .token_check_colon
    cmp al, '-'
    jbe .token_single
.token_check_colon:
    cmp al, ':'
    je .token_single
    cmp al, '='
    je .token_single
    cmp al, '['
    je .token_single
    cmp al, ']'
    je .token_single
    cmp al, '0'
    jb .token_check_ident
    cmp al, '9'
    jbe .token_numeric_run
.token_check_ident:
    cmp al, '_'
    je .token_ident_run
    cmp al, 'A'
    jb .token_single
    cmp al, 'Z'
    jbe .token_ident_run
    cmp al, 'a'
    jb .token_single
    cmp al, 'z'
    jbe .token_ident_run

.token_single:
    inc rdx
    jmp .token_end

    ; Un run commençant par un chiffre reste entier lexicalement candidat
    ; jusqu'au whitespace ou à une ponctuation. Le parseur signale ensuite
    ; précisément `_`, suffixe, hex invalide ou zéro initial en E102.
.token_numeric_run:
    inc rdx
    cmp rdx, r8
    jae .token_end
    mov al, [rcx + rdx]
    cmp al, 0x20
    je .token_end
    cmp al, 0x09
    je .token_end
    cmp al, '('
    jb .token_numeric_check_colon
    cmp al, '-'
    jbe .token_end
.token_numeric_check_colon:
    cmp al, ':'
    je .token_end
    cmp al, '='
    je .token_end
    cmp al, '['
    je .token_end
    cmp al, ']'
    je .token_end
    jmp .token_numeric_run

.token_ident_run:
    inc rdx
    cmp rdx, r8
    jae .token_end
    mov al, [rcx + rdx]
    cmp al, '_'
    je .token_ident_run
    cmp al, '0'
    jb .token_ident_upper
    cmp al, '9'
    jbe .token_ident_run
.token_ident_upper:
    cmp al, 'A'
    jb .token_ident_lower
    cmp al, 'Z'
    jbe .token_ident_run
.token_ident_lower:
    cmp al, 'a'
    jb .token_end
    cmp al, 'z'
    jbe .token_ident_run
.token_end:
    cmp r11d, 4
    je .remember_value_end
    cmp r11d, 5
    jne .token_skip_ws
    mov rax, [rbp + F_TOKEN_START]
    cmp byte [rcx + rax], '-'
    jne .token_skip_ws
.remember_value_end:
    mov [rbp + F_TOKEN_END], rdx
    jmp .token_skip_ws

.tokens_done:
    cmp r11d, 4
    jb .e201                        ; valeur absente, position EOF logique

    mov rdx, [rbp + F_TOKEN_START]
    mov r8, [rbp + F_TOKEN_END]
    mov al, [rcx + rdx]
    cmp al, '-'
    jne .unsigned_value

    mov qword [rbp + F_CONST_META], 0x0011
    lea r9, [rdx + 1]               ; magnitude exigée sans whitespace
    mov rdx, [rbp + F_EXTRA_START]
    cmp rdx, -1
    je .bad_signed_gap
    cmp rdx, r9
    jne .bad_signed_gap
    jmp .value_dispatch

.bad_signed_gap:
    mov rdx, r9                     ; whitespace ou EOF logique après '-'
    jmp .e102

.unsigned_value:
    cmp al, '+'
    je .e201
.value_dispatch:
    mov al, [rcx + rdx]
    cmp al, '0'
    je .value_zero
    cmp al, '1'
    jb .e102
    cmp al, '9'
    ja .e102

    xor eax, eax
.decimal_loop:
    cmp rdx, r8
    jae .value_parsed
    movzx r11d, byte [rcx + rdx]
    sub r11d, '0'
    cmp r11d, 9
    ja .e102
    mov r9, 0x1999999999999999
    mov r10d, 5
    cmp qword [rbp + F_CONST_META], 0x0011
    jne .decimal_bound_ready
    mov r9, 0x0CCCCCCCCCCCCCCC
    mov r10d, 8
.decimal_bound_ready:
    cmp rax, r9
    ja .e102
    jb .decimal_safe
    cmp r11d, r10d
    ja .e102
.decimal_safe:
    imul rax, rax, 10
    add rax, r11
    inc rdx
    jmp .decimal_loop

.value_zero:
    xor eax, eax
    inc rdx
    cmp rdx, r8
    jae .value_parsed
    cmp byte [rcx + rdx], 'x'
    jne .e102                      ; leading zero ou suffixe
    inc rdx
    cmp rdx, r8
    jae .e102                      ; 0x sans chiffre

.hex_loop:
    cmp rdx, r8
    jae .value_parsed
    movzx r11d, byte [rcx + rdx]
    cmp r11b, '0'
    jb .e102
    cmp r11b, '9'
    jbe .hex_digit
    cmp r11b, 'A'
    jb .hex_lower
    cmp r11b, 'F'
    jbe .hex_upper
.hex_lower:
    cmp r11b, 'a'
    jb .e102
    cmp r11b, 'f'
    ja .e102
    sub r11d, 'a' - 10
    jmp .hex_accumulate
.hex_upper:
    sub r11d, 'A' - 10
    jmp .hex_accumulate
.hex_digit:
    sub r11d, '0'
.hex_accumulate:
    mov r9, 0x0FFFFFFFFFFFFFFF
    mov r10d, 0x0F
    cmp qword [rbp + F_CONST_META], 0x0011
    jne .hex_bound_ready
    mov r9, 0x0800000000000000
    xor r10d, r10d
.hex_bound_ready:
    cmp rax, r9
    ja .e102
    jb .hex_safe
    cmp r11d, r10d
    ja .e102
.hex_safe:
    shl rax, 4
    or rax, r11
    inc rdx
    jmp .hex_loop

.value_parsed:
    cmp qword [rbp + F_CONST_META], 0x0011
    jne .value_bits_ready
    neg rax                         ; complément à deux modulo 2^64, -0 inclus
.value_bits_ready:
    mov [rbp + F_TOKEN_END], rax    ; slot réutilisé pour la valeur
    cmp qword [rbp + F_CONST_META], 0x0011
    jne .unsigned_extra
    mov rdx, [rbp + F_AFTER_START]
    jmp .check_extra
.unsigned_extra:
    mov rdx, [rbp + F_EXTRA_START]
.check_extra:
    cmp rdx, -1
    jne .e201                       ; second opérande/token

    ; Doublon byte-exact et sensible à la casse, avant le contrôle de capacité.
    xor r11d, r11d
.duplicate_next:
    cmp r11, [rbp + F_SYMBOL_COUNT]
    jae .duplicate_clear
    imul rax, r11, 48
    jo fail_e603
    mov r8, [rbp + F_WORK]
    add r8, 0x400
    jc fail_e603
    add r8, rax
    jc fail_e603
    mov r9d, dword [rbp + F_CONST_NAME_PACK]
    mov r10d, dword [rbp + F_CONST_NAME_PACK + 4]
    xor edx, edx
.duplicate_compare:
    cmp edx, r10d
    jae .duplicate_length
    mov al, [rcx + r9]
    cmp al, [r8 + rdx]
    jne .duplicate_miss
    inc r9d
    inc edx
    jmp .duplicate_compare
.duplicate_length:
    cmp byte [r8 + rdx], 0
    je .e300_name
.duplicate_miss:
    inc r11
    jmp .duplicate_next
.duplicate_clear:
    cmp r11, 512
    jae .e500_name
    imul rax, r11, 48
    jo .e500_name
    mov r8, [rbp + F_WORK]
    add r8, 0x400
    jc .e500_name
    add r8, rax
    jc .e500_name

%ifdef PRODUCE_PE
    SAVE_PASS_RECORD r8, 48
%endif
    mov r9d, dword [rbp + F_CONST_NAME_PACK]
    mov r10d, dword [rbp + F_CONST_NAME_PACK + 4]
    xor edx, edx
.copy_name:
    cmp edx, r10d
    jae .name_done
    mov al, [rcx + r9]
    mov [r8 + rdx], al
    inc r9d
    inc edx
    jmp .copy_name
.name_done:
    mov rax, [rbp + F_TOKEN_END]
    mov [r8 + 0x20], rax
    mov eax, dword [rbp + F_SEM_START]
    add eax, dword [rbp + F_CONST_NAME_PACK]
    mov [r8 + 0x28], eax
    mov rax, [rbp + F_SEM_LINE]
    mov [r8 + 0x2C], ax
    mov ax, [rbp + F_CONST_META]
    mov [r8 + 0x2E], ax
%ifdef PRODUCE_PE
    CHECK_PASS_RECORD r8, 48, .pass_mismatch
%endif
    inc r11
    mov [rbp + F_SYMBOL_COUNT], r11
    mov rax, [rbp + F_WORK]
    mov [rax], r11d
    xor eax, eax
    mov edx, 1                      ; constante insérée, continuer
    ret

%ifdef PRODUCE_PE
.pass_mismatch:
    mov rax, [rbp + F_SEM_BEGIN]
    inc rax
    mov edx, 600
    ret
%endif
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
.e102:
    lea rax, [rdx + 1]
    mov edx, 102
    ret
.e300_name:
    mov edx, dword [rbp + F_CONST_NAME_PACK]
    lea rax, [rdx + 1]
    mov edx, 300
    ret
.e500_name:
    mov edx, dword [rbp + F_CONST_NAME_PACK]
    lea rax, [rdx + 1]
    mov edx, 500
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
    %if (text_end - text_start) > PE_TEXT_RAW
        %error "B1.10 .text depasse PE_TEXT_RAW"
    %endif
    times PE_TEXT_RAW - ($ - $$) db 0

SECTION .rdata start=PE_RDATA_FILE vstart=PE_RDATA_RVA align=16
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
success_b112:
    ; NG_S0_T12_OK\r\n — succès temporaire du tracer B1.8.
    dw 'N','G','_','S','0','_','T','1','2','_','O','K',13,10,0
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
%ifdef B15_SNAPSHOT_MULTI
; Le padding final de .rdata héberge uniquement les données du build de preuve.
b15_multi_expected:
    db 'ZERO'
    times 28 db 0
    dq 0
    dd 30
    dw 3
    dw 0x0001
    db 'MAX'
    times 29 db 0
    dq 0xFFFFFFFFFFFFFFFF
    dd 45
    dw 4
    dw 0x0001
    db 'HEXMAX'
    times 26 db 0
    dq 0xFFFFFFFFFFFFFFFF
    dd 78
    dw 5
    dw 0x0001
%endif
%ifdef B16_SNAPSHOT_CASE
b16_snapshot_expected:
    db 'A'
    times 31 db 0
    %if B16_SNAPSHOT_CASE = 1
        dq 0xFFFFFFFFFFFFFFFF
        dd 30
        dw 3
        dw 0x0011
    %elif B16_SNAPSHOT_CASE = 2
        dq 0x8000000000000000
        dd 30
        dw 3
        dw 0x0011
    %elif B16_SNAPSHOT_CASE = 7
        dq 1
        dd 30
        dw 3
        dw 0x0001
        db 'a'
        times 31 db 0
        dq 0xFFFFFFFFFFFFFFFF
        dd 42
        dw 4
        dw 0x0011
    %elif B16_SNAPSHOT_CASE = 8
        dq 0
        dd 30
        dw 3
        dw 0x0011
    %endif
%endif
%ifdef B17_SNAPSHOT_CASE
b17_snapshot_expected:
    dq 1
    %if (B17_SNAPSHOT_CASE = 0) || (B17_SNAPSHOT_CASE = 5)
        dq 30, 5, 3, 24
    %elif B17_SNAPSHOT_CASE = 1
        dq 60, 5, 5, 54
    %elif B17_SNAPSHOT_CASE = 2
        dq 30, 31, 3, 24
    %elif B17_SNAPSHOT_CASE = 3
        dq 45, 8, 4, 39
    %elif B17_SNAPSHOT_CASE = 4
        dq 46, 5, 4, 40
    %endif
%endif
%ifdef B18_SNAPSHOT
b18_state_expected:               ; seen, off, len, line, line_start, text_offset.
    dq 1, 52, 8, 6, 46, 0        ; Valeurs figées depuis les octets C01 avant code.
%endif
b18_body_words:                   ; Records {u8 longueur, u16 diagnostic, octets}.
    db 7                          ; Longueur du mot section.
    dw 200                        ; Diagnostic si ce mot ouvre le corps texte.
b18_section_kw:                   ; Ce même mot sert au parseur d'en-tête.
    db 'section'                   ; Mot exact, sans terminateur.
    db 6                          ; Longueur du mot format.
    dw 200                        ; Diagnostic si ce mot ouvre le corps texte.
    db 'format'                   ; Mot exact, sans terminateur.
    db 3                          ; Longueur du mot abi.
    dw 200                        ; Diagnostic si ce mot ouvre le corps texte.
    db 'abi'                   ; Mot exact, sans terminateur.
    db 5                          ; Longueur du mot const.
    dw 200                        ; Diagnostic si ce mot ouvre le corps texte.
    db 'const'                   ; Mot exact, sans terminateur.
    db 5                          ; Longueur du mot entry.
    dw 200                        ; Diagnostic si ce mot ouvre le corps texte.
    db 'entry'                   ; Mot exact, sans terminateur.
    db 4                          ; Longueur du mot byte.
    dw 202                        ; Diagnostic si ce mot ouvre le corps texte.
    db 'byte'                   ; Mot exact, sans terminateur.
    db 4                          ; Longueur du mot word.
    dw 202                        ; Diagnostic si ce mot ouvre le corps texte.
    db 'word'                   ; Mot exact, sans terminateur.
    db 5                          ; Longueur du mot dword.
    dw 202                        ; Diagnostic si ce mot ouvre le corps texte.
    db 'dword'                   ; Mot exact, sans terminateur.
    db 5                          ; Longueur du mot qword.
    dw 202                        ; Diagnostic si ce mot ouvre le corps texte.
    db 'qword'                   ; Mot exact, sans terminateur.
    db 5                          ; Longueur du mot ascii.
    dw 202                        ; Diagnostic si ce mot ouvre le corps texte.
    db 'ascii'                   ; Mot exact, sans terminateur.
    db 6                          ; Longueur du mot utf16z.
    dw 202                        ; Diagnostic si ce mot ouvre le corps texte.
    db 'utf16z'                   ; Mot exact, sans terminateur.
    db 4                          ; Longueur du mot zero.
    dw 202                        ; Diagnostic si ce mot ouvre le corps texte.
    db 'zero'                   ; Mot exact, sans terminateur.
    db 0                          ; Sentinelle finale, aucune entrée suivante.
b18_text_kw:
    db '.text'                    ; Nom exact de la première section.
b18_rx_kw:
    db 'rx'                       ; Droits lecture/exécution du texte.
b19_rdata_kw:
    db '.rdata'
b19_r_kw:
    db 'r'
b19_data_words:                   ; Longueur u8, diagnostic u16, mot ASCII.
    db 7
    dw 200
    db 'section'
    db 6
    dw 200
    db 'format'
    db 3
    dw 200
    db 'abi'
    db 5
    dw 200
    db 'const'
    db 5
    dw 200
    db 'entry'
    db 3
    dw 202
    db 'mov'
    db 3
    dw 202
    db 'lea'
    db 5
    dw 202
    db 'load8'
    db 6
    dw 202
    db 'load16'
    db 6
    dw 202
    db 'load32'
    db 6
    dw 202
    db 'load64'
    db 6
    dw 202
    db 'store8'
    db 7
    dw 202
    db 'store16'
    db 7
    dw 202
    db 'store32'
    db 7
    dw 202
    db 'store64'
    db 3
    dw 202
    db 'add'
    db 3
    dw 202
    db 'sub'
    db 3
    dw 202
    db 'and'
    db 2
    dw 202
    db 'or'
    db 3
    dw 202
    db 'xor'
    db 3
    dw 202
    db 'cmp'
    db 4
    dw 202
    db 'test'
    db 3
    dw 202
    db 'shl'
    db 3
    dw 202
    db 'shr'
    db 4
    dw 202
    db 'imul'
    db 4
    dw 202
    db 'push'
    db 3
    dw 202
    db 'pop'
    db 4
    dw 202
    db 'call'
    db 3
    dw 202
    db 'ret'
    db 3
    dw 202
    db 'jmp'
    db 2
    dw 202
    db 'je'
    db 3
    dw 202
    db 'jne'
    db 2
    dw 202
    db 'jb'
    db 3
    dw 202
    db 'jae'
    db 3
    dw 202
    db 'nop'
    db 0                          ; Terminaison de table.
b110_data_kw:
    db '.data'                    ; Nom de la troisième section.
b110_rw_kw:
    db 'rw'                       ; Lecture/écriture, jamais exécutable.
%ifdef B111_SNAPSHOT
b111_expected_symbols:
    db 'start'
    times 27 db 0
    dq 0
    dd 53
    dw 5, 0x22
    db 'ro'
    times 30 db 0
    dq 0
    dd 77
    dw 7, 0x42
    db 'rw'
    times 30 db 0
    dq 0
    dd 98
    dw 9, 0x62
%endif
%ifdef SCALAR_SNAPSHOT
scalar_expected:
    %if SCALAR_SNAPSHOT = 0
        dq 4, 4, 0
        db 0x00,0xFF,0x80,0xFF
    %elif SCALAR_SNAPSHOT = 1
        dq 8, 8, 0
        db 0x00,0x00,0xFF,0xFF,0x00,0x80,0xFF,0xFF
    %elif SCALAR_SNAPSHOT = 2
        dq 16, 16, 0
        dd 0, 0xFFFFFFFF, 0x80000000, 0xFFFFFFFF
    %elif SCALAR_SNAPSHOT = 3
        dq 32, 32, 0
        dq 0, 0xFFFFFFFFFFFFFFFF, 0x8000000000000000, 0xFFFFFFFFFFFFFFFF
    %elif SCALAR_SNAPSHOT = 4
        dq 8, 16, 0
        dq 0xFFFFFFFFFFFFFFFF
    %elif SCALAR_SNAPSHOT = 5
        dq 8, 16, 0
        dq 4
    %else
        %error "SCALAR_SNAPSHOT doit valoir 0..5"
    %endif
%endif
%ifdef TEXT_SNAPSHOT
text_expected:
    %if TEXT_SNAPSHOT = 0
        dq 3, 1, 3, 0
        db 0x41,0x3B,0x42
    %elif TEXT_SNAPSHOT = 1
        dq 11, 1, 11, 0
        db 0x41,0x5C,0x42,0x22,0x43,0x0A,0x0D,0x09,0x00,0x7F,0xFF
    %elif TEXT_SNAPSHOT = 2
        dq 8, 1, 8, 0
        db 0x41,0,0x3B,0,0x42,0,0,0
    %elif TEXT_SNAPSHOT = 3
        dq 0, 1, 0, 0
    %elif TEXT_SNAPSHOT = 4
        dq 2, 1, 2, 0
        db 0,0
    %elif TEXT_SNAPSHOT = 5
        dq 3, 1, 3, 0
        db 0x61,0,0x62
    %elif TEXT_SNAPSHOT = 6
        dq 8192, 2, 0, 8192
    %elif TEXT_SNAPSHOT = 7
        dq 16384, 2, 16384, 0
    %elif TEXT_SNAPSHOT = 8
        dq 8, 1, 16, 0
        dq 1
    %else
        %error "TEXT_SNAPSHOT doit valoir 0..8"
    %endif
%endif
gpr64_names:
    db 'raxrcxrdxrbxrsprbprsirdi'
branch_instructions:
    db 4, 1, 0xE8, 0, 'call'
    db 3, 1, 0xE9, 0, 'jmp'
    db 2, 2, 0x84, 0, 'je'
    db 3, 2, 0x85, 0, 'jne'
    db 2, 2, 0x82, 0, 'jb'
    db 3, 2, 0x83, 0, 'jae'
    db 0
memory_instructions:
    db 3, 0x48, 0x8D, 0, 'lea'
    db 5, 0x40, 0xB6, 4, 'load8'
    db 6, 0x40, 0xB7, 4, 'load16'
    db 6, 0x40, 0x8B, 0, 'load32'
    db 6, 0x48, 0x8B, 0, 'load64'
    db 6, 0x40, 0x88, 1, 'store8'
    db 7, 0x40, 0x89, 3, 'store16'
    db 7, 0x40, 0x89, 1, 'store32'
    db 7, 0x48, 0x89, 1, 'store64'
    db 0
register_instructions:            ; Longueur, forme, opcode, extension, nom.
    db 3, 0, 0x90, 0, 'nop'
    db 3, 0, 0xc3, 0, 'ret'
    db 4, 1, 0x50, 0, 'push'
    db 3, 1, 0x58, 0, 'pop'
    db 4, 2, 0xff, 2, 'call'
    db 3, 3, 0x89, 0, 'mov'
    db 3, 4, 0x1, 0, 'add'
    db 3, 4, 0x29, 5, 'sub'
    db 3, 4, 0x21, 4, 'and'
    db 2, 4, 0x9, 1, 'or'
    db 3, 4, 0x31, 6, 'xor'
    db 3, 4, 0x39, 7, 'cmp'
    db 4, 5, 0x85, 0, 'test'
    db 3, 6, 0xc1, 4, 'shl'
    db 3, 6, 0xc1, 5, 'shr'
    db 4, 7, 0x69, 0, 'imul'
    db 0
%ifdef INSN_SNAPSHOT
instruction_expected:
%if INSN_SNAPSHOT = 2
    db 0x48, 0x8b, 0x00, 0x48, 0x8b, 0x01, 0x48, 0x8b, 0x02, 0x48, 0x8b, 0x03, 0x48, 0x8b, 0x04, 0x24
    db 0x48, 0x8b, 0x45, 0x00, 0x48, 0x8b, 0x06, 0x48, 0x8b, 0x07, 0x49, 0x8b, 0x00, 0x49, 0x8b, 0x01
    db 0x49, 0x8b, 0x02, 0x49, 0x8b, 0x03, 0x49, 0x8b, 0x04, 0x24, 0x49, 0x8b, 0x45, 0x00, 0x49, 0x8b
    db 0x06, 0x49, 0x8b, 0x07, 0x4d, 0x8d, 0x5c, 0x9c, 0x20, 0x40, 0x0f, 0xb6, 0x06, 0x45, 0x0f, 0xb7
    db 0x4d, 0x00, 0x47, 0x8b, 0x54, 0xcb, 0x80, 0x40, 0x88, 0x47, 0x01, 0x66, 0x45, 0x89, 0x45, 0x00
    db 0x44, 0x89, 0x54, 0x24, 0x08, 0x4f, 0x89, 0xb4, 0x7c, 0x80, 0x00, 0x00, 0x00, 0x48, 0x8d, 0x43
    db 0x7f, 0x48, 0x8d, 0x83, 0x80, 0x00, 0x00, 0x00, 0x48, 0x8d, 0x43, 0x80, 0x48, 0x8d, 0x83, 0x7f
    db 0xff, 0xff, 0xff, 0x48, 0x8d, 0x83, 0xff, 0xff, 0xff, 0x7f, 0x48, 0x8d, 0x83, 0x00, 0x00, 0x00
    db 0x80, 0x48, 0x8d, 0x03, 0x48, 0x8d, 0x43, 0xf8, 0x48, 0x8d, 0x04, 0x10, 0x48, 0x8d, 0x04, 0x50
    db 0x48, 0x8d, 0x04, 0x90, 0x48, 0x8d, 0x04, 0xd0
%else
%ifdef INSN_SNAPSHOT_CORRUPT
    db 0x91                       ; Oracle volontairement faux : le garde doit refuser.
%else
    db 0x90
%endif
    db 0xc3, 0x50, 0x41, 0x57, 0x5d, 0x41, 0x5c, 0xff, 0xd0, 0x41, 0xff, 0xd2, 0x48, 0x89, 0xf8, 0x4c
    db 0x89, 0xc1, 0x4c, 0x89, 0xca, 0x4c, 0x89, 0xd3, 0x4c, 0x89, 0xdc, 0x4c, 0x89, 0xe5, 0x4c, 0x89
    db 0xee, 0x4c, 0x89, 0xf7, 0x4d, 0x89, 0xf8, 0x49, 0x89, 0xc1, 0x49, 0x89, 0xca, 0x49, 0x89, 0xd3
    db 0x49, 0x89, 0xdc, 0x49, 0x89, 0xe5, 0x49, 0x89, 0xee, 0x49, 0x89, 0xf7, 0x48, 0xb8, 0x00, 0x00
    db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x49, 0xb8, 0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01
    db 0x49, 0xbf, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x48, 0xb9, 0xff, 0xff, 0xff, 0xff
    db 0xff, 0xff, 0xff, 0xff, 0x4d, 0x01, 0xd1, 0x49, 0x81, 0xc1, 0x00, 0x00, 0x00, 0x80, 0x4d, 0x29
    db 0xd1, 0x49, 0x81, 0xe9, 0x00, 0x00, 0x00, 0x80, 0x4d, 0x21, 0xd1, 0x49, 0x81, 0xe1, 0x00, 0x00
    db 0x00, 0x80, 0x4d, 0x09, 0xd1, 0x49, 0x81, 0xc9, 0x00, 0x00, 0x00, 0x80, 0x4d, 0x31, 0xd1, 0x49
    db 0x81, 0xf1, 0x00, 0x00, 0x00, 0x80, 0x4d, 0x39, 0xd1, 0x49, 0x81, 0xf9, 0x00, 0x00, 0x00, 0x80
    db 0x4c, 0x85, 0xeb, 0x49, 0xc1, 0xe0, 0x3f, 0x49, 0xc1, 0xef, 0x01, 0x48, 0xc1, 0xe0, 0x00, 0x4d
    db 0x69, 0xc1, 0xfe, 0xff, 0xff, 0xff, 0x48, 0x81, 0xc0, 0xff, 0xff, 0xff, 0xff
%endif
instruction_expected_end:
%endif

%ifdef FIXUP_SNAPSHOT
fixup_expected:
%if FIXUP_SNAPSHOT = 2
    db 0x53, 0x48, 0x81, 0xec, 0x20, 0x00, 0x00, 0x00, 0x48, 0x89, 0xd3, 0x48, 0x8b, 0x4b, 0x40, 0x48
    db 0x8b, 0x41, 0x08, 0x48, 0x8d, 0x15, 0xee, 0x0f, 0x00, 0x00, 0xff, 0xd0, 0x48, 0x81, 0xc4, 0x20
    db 0x00, 0x00, 0x00, 0x5b, 0x48, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc3
%else
    db 0xe8, 0x00, 0x00, 0x00, 0x00, 0x90, 0xe8, 0xfb, 0xff, 0xff, 0xff, 0xe9, 0x00, 0x00, 0x00, 0x00
    db 0x90, 0xe9, 0xfb, 0xff, 0xff, 0xff, 0x0f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x90, 0x0f, 0x84, 0xfa
    db 0xff, 0xff, 0xff, 0x0f, 0x85, 0x00, 0x00, 0x00, 0x00, 0x90, 0x0f, 0x85, 0xfa, 0xff, 0xff, 0xff
    db 0x0f, 0x82, 0x00, 0x00, 0x00, 0x00, 0x90, 0x0f, 0x82, 0xfa, 0xff, 0xff, 0xff, 0x0f, 0x83, 0x00
    db 0x00, 0x00, 0x00, 0x90, 0x0f, 0x83, 0xfa, 0xff, 0xff, 0xff, 0x48, 0x8d, 0x15, 0xaf, 0x0f, 0x00
    db 0x00, 0x44, 0x0f, 0xb6, 0x0d, 0xa7, 0x0f, 0x00, 0x00, 0x44, 0x0f, 0xb7, 0x15, 0x9f, 0x0f, 0x00
    db 0x00, 0x4c, 0x8b, 0x3d, 0xf9, 0xff, 0xff, 0xff, 0x66, 0x44, 0x89, 0x05, 0x90, 0x1f, 0x00, 0x00
    db 0x48, 0x89, 0x05, 0x89, 0x1f, 0x00, 0x00
%endif
fixup_expected_end:
%endif

%ifdef ADDR_SNAPSHOT
address_expected:
    dd 92, 0
    dw 8, 12, 5, 2
    dd 105, 8
    dw 8, 25, 2, 2
    dd 115, 16
    dw 8, 35, 5, 2
address_expected_end:
%endif

%ifdef PRODUCE_PE
output_filename:
    dw 'N','T','A','S','M','1','.','E','F','I',0
producer_success:
    dw 'N','G','_','N','T','A','S','M','_','S','0','_','O','K',13,10,0
%endif

rdata_end:
    times PE_RDATA_RAW - ($ - $$) db 0

SECTION .data start=PE_DATA_FILE vstart=PE_DATA_RVA align=8
data_start:
    dq 0
data_end:
    times PE_DATA_RAW - ($ - $$) db 0

SECTION .reloc start=PE_RELOC_FILE vstart=PE_RELOC_RVA align=4
reloc_start:
    dd PE_RDATA_RVA
    dd 12
    dw 0xA000 | (reloc_anchor - rdata_start)
    dw 0
reloc_end:
    times PE_RELOC_RAW - ($ - $$) db 0
