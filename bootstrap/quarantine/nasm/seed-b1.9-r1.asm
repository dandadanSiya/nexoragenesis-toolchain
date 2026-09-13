; BOOTSTRAP_EXTERNAL_ASM
; Quarantaine temporaire autorisée pour le tracer B1.9 uniquement.
; Commande de matérialisation quarantainée :
; nasm -f bin bootstrap/quarantine/nasm/seed-b1.9-r1.asm -o bootstrap/quarantine/nasm/out/seed-b1.9-r1.efi

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
    dd 0x2000
    dd 0x0A00
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
    dd 0x2000
    dd 0x0400
    dd 0,0
    dw 0,0
    dd 0x60000020

    db '.rdata',0,0
    dd rdata_end - rdata_start
    dd 0x3000
    dd 0x0600
    dd 0x2400
    dd 0,0
    dw 0,0
    dd 0x40000040

    db '.data',0,0,0
    dd data_end - data_start
    dd 0x4000
    dd 0x0200
    dd 0x2A00
    dd 0,0
    dw 0,0
    dd 0xC0000040

    db '.reloc',0,0
    dd reloc_end - reloc_start
    dd 0x5000
    dd 0x0200
    dd 0x2C00
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
    mov qword [rbp + F_SYMBOL_COUNT], 0
    mov rax, [rbp + F_WORK]
    mov dword [rax], 0

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
    mov r9, [rbp + F_SEM_STAGE]
    cmp r9d, CONST_OR_ENTRY
    je semantic_const_or_entry
    cmp r9d, AFTER_ENTRY
    je semantic_after_entry
    cmp r9d, IN_TEXT                ; Le cinquième état classe le corps texte.
    je semantic_in_text             ; Aucun statement du corps n'est encore accepté.
    cmp r9d, IN_RDATA                ; Nouvel état de la section en lecture seule.
    je semantic_in_rdata
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
    jne .name_ready
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
    jne .rights_ready
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
    mov qword [rbp + F_TEXT_OFFSET], 0 ; Aucun encodeur ne mesure encore du code.
    mov qword [rbp + F_SEM_STAGE], IN_TEXT ; Fermer définitivement l'en-tête texte.
    jmp semantic_next_record      ; Continuer à détecter les statements interdits.
.enter_rdata:
    mov rax, [rbp + F_WORK]        ; État réservé au début du pool de travail.
    mov qword [rax + 8], 0         ; Offset rdata nul tant que les directives sont absentes.
    mov qword [rbp + F_SEM_STAGE], IN_RDATA
    jmp semantic_next_record      ; Examiner toutes les lignes suivantes.
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
    jne .bad_second_position
    lea r8, [rel b18_text_kw]      ; Préserver le diagnostic historique de .text dupliquée.
    mov r9d, 5
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
    lea r9, [rel b19_data_words]   ; Instructions interdites dans les données : E202.
    call b18_body_error
    jmp semantic_const_error

semantic_success:
    mov rax, [rbp + F_WORK]
    cmp qword [rax + 8], 0         ; Aucun payload rdata ne doit encore être compté.
    jne fail_e603
    cmp qword [rbp + F_TEXT_OFFSET], 0 ; Sans encodeur, aucun octet ne doit être compté.
    jne fail_e603                  ; Un offset non nul trahirait un état incohérent.
    call validate_entry_descriptor
    test eax, eax
    jnz fail_e603
    jmp cleanup

semantic_eof:
    cmp qword [rbp + F_SEM_STAGE], IN_RDATA ; EOF après rdata est le succès provisoire T09.
    je semantic_success
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
    lea rdx, [rel success_b19]
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
b18_lex_header:
    mov r9, [rbp + F_WORK]         ; Pool WORK validé et encore possédé.
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
    cmp al, '.'                   ; Le point initial appartient au token section.
    jne .word
    inc rdx                       ; Inclure le point puis scanner son suffixe.
    jmp .word
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
    cmp qword [rbp + F_SEM_STAGE], IN_RDATA ; Le succès exige les deux en-têtes.
    jne .bad
    cmp qword [rbp + F_ENTRY_SEEN], 1
    jne .bad
    mov rcx, [rbp + F_ENTRY_NAME_LEN]
    test rcx, rcx
    jz .bad
    cmp rcx, 31
    ja .bad
    mov rax, [rbp + F_ENTRY_NAME_OFF]
    cmp rax, [rbp + F_SOURCE_LEN]
    jae .bad
    mov rdx, rax
    add rdx, rcx
    jc .bad
    cmp rdx, [rbp + F_SOURCE_LEN]
    ja .bad
    cmp rax, [rbp + F_ENTRY_LINE_START]
    jb .bad
    mov rax, [rbp + F_ENTRY_LINE]
    test rax, rax
    jz .bad
    cmp rax, [rbp + F_LINES_DONE]
    ja .bad
    xor eax, eax
    ret
.bad:
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
    inc r11
    mov [rbp + F_SYMBOL_COUNT], r11
    mov rax, [rbp + F_WORK]
    mov [rax], r11d
    xor eax, eax
    mov edx, 1                      ; constante insérée, continuer
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
    %if (text_end - text_start) > 0x2000
        %error "B1.8 .text depasse 0x2000"
    %endif
    times 0x2000 - ($ - $$) db 0

SECTION .rdata start=0x2400 vstart=0x3000 align=16
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
success_b19:
    ; NG_S0_T09_OK\r\n — succès temporaire du tracer B1.8.
    dw 'N','G','_','S','0','_','T','0','9','_','O','K',13,10,0
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
rdata_end:
    times 0x600 - ($ - $$) db 0

SECTION .data start=0x2A00 vstart=0x4000 align=8
data_start:
    dq 0
data_end:
    times 0x200 - ($ - $$) db 0

SECTION .reloc start=0x2C00 vstart=0x5000 align=4
reloc_start:
    dd 0x3000
    dd 12
    dw 0xA000 | (reloc_anchor - rdata_start)
    dw 0
reloc_end:
    times 0x200 - ($ - $$) db 0
