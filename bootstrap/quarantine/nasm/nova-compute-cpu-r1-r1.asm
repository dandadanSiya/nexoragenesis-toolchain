; BOOTSTRAP_EXTERNAL_ASM
; NOVA_COMPUTE_R1_EXTERNAL_PROBE -- UEFI pre-kernel, QEMU only, NON-RELEASE.
; External NASM quarantine probe. It reads no Nova source and is not an OS,
; kernel, release artifact, or dependency of the final Nexora Genesis build.
;
; Canonical build (NASM 2.16.01, WSL):
; nasm -f bin -w+error bootstrap/quarantine/nasm/nova-compute-cpu-r1-r1.asm \
;   -o bootstrap/quarantine/nasm/out/tdd/nova-compute-cpu-r1-r1.efi
;
; Stable fault macros (one per build):
;   R1_FAULT_RESULT       -> E201_RESULT
;   R1_FAULT_SUFFIX       -> E203_SUFFIX
;   R1_FAULT_CORPUS       -> E204_CORPUS
;   R1_FAULT_DIGEST       -> E205_DIGEST
;   R1_FAULT_GUARD_LO     -> E202_GUARD at N=0
;   R1_FAULT_GUARD_HI     -> E202_GUARD at N=256
; The two guard routes separately prove both buffer boundaries.

BITS 64
ORG 0

%define IMAGE_BASE              0x0000000140000000
%define EFI_SUCCESS             0
%define EFI_DEVICE_ERROR        0x8000000000000007
%define EFI_ABORTED             0x8000000000000015
%define EFI_COMPROMISED_DATA    0x8000000000000021
%define VECTOR_CAPACITY         256
%define CASE_COUNT              7
%define OUTPUT_SENTINEL         0xA5A5A5A5
%define GUARD_PRE_VALUE         0xC0DEC0DE
%define GUARD_POST_VALUE        0xDEC0ADDE
%define FNV_OFFSET_BASIS        0xCBF29CE484222325
%define FNV_PRIME               0x00000100000001B3
%define FNV_A                   0xC3F35DC2B53DEA64
%define FNV_B                   0x2473B19E192A0FE7
%define FNV_G                   0x558F555BEC217985
%define FNV_CORPUS              0x69EEB57856E393CE
%define FNV_RUN                 0x7CF3026858C7AC7C

; Fault builds are single-cause experiments. Defaults make a numeric sum
; possible without any table-generating preprocessor loop.
%ifndef R1_FAULT_RESULT
    %define R1_FAULT_RESULT 0
%endif
%ifndef R1_FAULT_SUFFIX
    %define R1_FAULT_SUFFIX 0
%endif
%ifndef R1_FAULT_CORPUS
    %define R1_FAULT_CORPUS 0
%endif
%ifndef R1_FAULT_DIGEST
    %define R1_FAULT_DIGEST 0
%endif
%ifndef R1_FAULT_GUARD_LO
    %define R1_FAULT_GUARD_LO 0
%endif
%ifndef R1_FAULT_GUARD_HI
    %define R1_FAULT_GUARD_HI 0
%endif
%if (R1_FAULT_RESULT + R1_FAULT_SUFFIX + R1_FAULT_CORPUS + R1_FAULT_DIGEST + R1_FAULT_GUARD_LO + R1_FAULT_GUARD_HI) > 1
    %error "NC-R1 fault macros are mutually exclusive"
%endif

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
    dd 0x0400
    dd 0x1800
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
    dd 0x0400
    dd 0x0400
    dd 0,0
    dw 0,0
    dd 0x60000020

    db '.rdata',0,0
    dd rdata_end - rdata_start
    dd 0x2000
    dd 0x1000
    dd 0x0800
    dd 0,0
    dw 0,0
    dd 0x40000040

    db '.data',0,0,0
    dd data_end - data_start
    dd 0x3000
    dd 0x0600
    dd 0x1800
    dd 0,0
    dw 0,0
    dd 0xC0000040

    db '.reloc',0,0
    dd reloc_end - reloc_start
    dd 0x4000
    dd 0x0200
    dd 0x1E00
    dd 0,0
    dw 0,0
    dd 0x42000040

    times 0x0400 - ($ - $$) db 0

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

    ; The immutable corpus is checked before execution. The case list is part
    ; of that corpus; per-table FNV values localize drift, while A||B||G fixes
    ; order and boundaries.
    lea r8, [rel case_counts]
    cmp word [r8 + 0], 0
    jne .corpus_fail
    cmp word [r8 + 2], 1
    jne .corpus_fail
    cmp word [r8 + 4], 15
    jne .corpus_fail
    cmp word [r8 + 6], 16
    jne .corpus_fail
    cmp word [r8 + 8], 17
    jne .corpus_fail
    cmp word [r8 + 10], 255
    jne .corpus_fail
    cmp word [r8 + 12], 256
    jne .corpus_fail

    mov rax, FNV_OFFSET_BASIS
    lea r8, [rel vector_a]
    mov ecx, 1024
    call fnv_bytes
    mov rdx, FNV_A
    cmp rax, rdx
    jne .corpus_fail

    mov rax, FNV_OFFSET_BASIS
    lea r8, [rel vector_b]
    mov ecx, 1024
    call fnv_bytes
    mov rdx, FNV_B
    cmp rax, rdx
    jne .corpus_fail

    mov rax, FNV_OFFSET_BASIS
    lea r8, [rel vector_g]
    mov ecx, 1024
    call fnv_bytes
    mov rdx, FNV_G
    cmp rax, rdx
    jne .corpus_fail

    mov rax, FNV_OFFSET_BASIS
    lea r8, [rel vector_a]
    mov ecx, 3072
    call fnv_bytes
    mov rdx, FNV_CORPUS
    cmp rax, rdx
    jne .corpus_fail

    mov rax, FNV_OFFSET_BASIS
    mov [rel run_digest], rax
    mov dword [rel stream_size], 0
    mov dword [rel case_index], 0

.case_loop:
    mov edx, [rel case_index]
    cmp edx, CASE_COUNT
    jae .cases_done
    lea r8, [rel case_counts]
    movzx r11d, word [r8 + rdx * 2]
    mov [rel current_n], r11w

    mov dword [rel guard_pre], GUARD_PRE_VALUE
    mov dword [rel guard_post], GUARD_POST_VALUE
    lea r8, [rel out_vec]
    mov ecx, VECTOR_CAPACITY
    mov eax, OUTPUT_SENTINEL
.fill_loop:
    mov [r8], eax
    add r8, 4
    dec ecx
    jnz .fill_loop

%if R1_FAULT_GUARD_LO
    ; Pre-dispatch injection: prove the low guard at the N=0 boundary before
    ; entering any vector-add execution path.
    test r11d, r11d
    jnz .fault_guard_lo_done
    xor dword [rel guard_pre], 1
.fault_guard_lo_done:
%endif

    lea r8, [rel vector_a]
    lea r9, [rel vector_b]
    lea r10, [rel out_vec]
    xor ecx, ecx
.compute_loop:
    cmp ecx, r11d
    jae .compute_done
    mov eax, [r8 + rcx * 4]
    add eax, [r9 + rcx * 4]
    mov [r10 + rcx * 4], eax
    inc ecx
    jmp .compute_loop

.compute_done:
%if R1_FAULT_RESULT
    cmp r11d, 17
    jne .fault_result_done
    xor dword [rel out_vec + 16 * 4], 1
.fault_result_done:
%endif
%if R1_FAULT_GUARD_HI
    cmp r11d, VECTOR_CAPACITY
    jne .fault_guard_hi_done
    xor dword [rel guard_post], 1
.fault_guard_hi_done:
%endif
%if R1_FAULT_SUFFIX
    cmp r11d, 16
    jne .fault_suffix_done
    xor dword [rel out_vec + 16 * 4], 1
.fault_suffix_done:
%endif
    cmp dword [rel guard_pre], GUARD_PRE_VALUE
    jne .guard_fail
    cmp dword [rel guard_post], GUARD_POST_VALUE
    jne .guard_fail

    lea r8, [rel out_vec]
    lea r9, [rel vector_g]
    xor ecx, ecx
.verify_result_loop:
    cmp ecx, r11d
    jae .verify_suffix
    mov eax, [r8 + rcx * 4]
    cmp eax, [r9 + rcx * 4]
    jne .result_fail
    inc ecx
    jmp .verify_result_loop

.verify_suffix:
    mov ecx, r11d
.verify_suffix_loop:
    cmp ecx, VECTOR_CAPACITY
    jae .case_digest
    cmp dword [r8 + rcx * 4], OUTPUT_SENTINEL
    jne .suffix_fail
    inc ecx
    jmp .verify_suffix_loop

.case_digest:
    mov rax, [rel run_digest]
    lea r8, [rel current_n]
    mov ecx, 2
    call fnv_bytes
    lea r8, [rel out_vec]
    movzx ecx, word [rel current_n]
    shl ecx, 2
    call fnv_bytes
    mov [rel run_digest], rax
    movzx eax, word [rel current_n]
    shl eax, 2
    add eax, 2
    add [rel stream_size], eax
    jc .digest_fail
    inc dword [rel case_index]
    jmp .case_loop

.cases_done:
    cmp dword [rel stream_size], 2254
    jne .digest_fail
    mov rax, [rel run_digest]
%if R1_FAULT_DIGEST
    xor rax, 1
%endif
    mov rdx, FNV_RUN
    cmp rax, rdx
    jne .digest_fail

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
    jmp .report
.suffix_fail:
    mov rax, EFI_COMPROMISED_DATA
    mov [rsp + 0x30], rax
    lea rdx, [rel msg_e203]
    jmp .report
.corpus_fail:
    mov rax, EFI_COMPROMISED_DATA
    mov [rsp + 0x30], rax
    lea rdx, [rel msg_e204]
    jmp .report
.digest_fail:
    mov rax, EFI_COMPROMISED_DATA
    mov [rsp + 0x30], rax
    lea rdx, [rel msg_e205]

.report:
    mov rcx, [rsp + 0x20]
    mov rax, [rsp + 0x28]
    call rax
    test rax, rax
    jz .report_ok
    mov rax, EFI_DEVICE_ERROR
    jmp .epilogue
.report_ok:
    mov rax, [rsp + 0x30]
    jmp .epilogue

.no_console:
    mov rax, EFI_DEVICE_ERROR
.epilogue:
    add rsp, 0x38
    ret

; FNV-1a64 over ECX bytes at R8, seeded by RAX. Low-64-bit IMUL provides
; the required modulo-2^64 multiplication and byte order is memory order.
fnv_bytes:
    mov r10, FNV_PRIME
    test ecx, ecx
    jz .done
.loop:
    movzx r9d, byte [r8]
    xor rax, r9
    imul rax, r10
    inc r8
    dec ecx
    jnz .loop
.done:
    ret

text_end:
    %if (text_end - text_start) > 0x0400
        %error "NC-R1 .text exceeds 0x400"
    %endif
    times 0x0400 - ($ - $$) db 0

SECTION .rdata start=0x0800 vstart=0x2000 align=16
rdata_start:
; Directed lanes 0..16 are explicit. Lanes 17..255 are emitted as immutable
; dwords from the frozen corpus formula at assembly time (never at runtime).
vector_a:
    dd 0x00000000
    dd 0x00000001
    dd 0xFFFFFFFF
    dd 0x80000000
    dd 0x7FFFFFFF
    dd 0x12345678
    dd 0xAAAAAAAA
    dd 0xDEADBEEF
    dd 0xFFFFFFFF
    dd 0x80000000
    dd 0x0000FFFF
    dd 0x00FFFFFF
    dd 0x01020304
    dd 0x13579BDF
    dd 0x40000000
    dd 0xCAFEBABE
    dd 0x00000001
%if R1_FAULT_CORPUS
    ; Canonical A[17] with its low bit inverted.
    dd 0xA5EE7FD0
%else
    dd 0xA5EE7FD1
%endif
    dd 0x4425F98A
    dd 0xE25D7343
    dd 0x8094ECFC
    dd 0x1ECC66B5
    dd 0xBD03E06E
    dd 0x5B3B5A27
    dd 0xF972D3E0
    dd 0x97AA4D99
    dd 0x35E1C752
    dd 0xD419410B
    dd 0x7250BAC4
    dd 0x1088347D
    dd 0xAEBFAE36
    dd 0x4CF727EF
    dd 0xEB2EA1A8
    dd 0x89661B61
    dd 0x279D951A
    dd 0xC5D50ED3
    dd 0x640C888C
    dd 0x02440245
    dd 0xA07B7BFE
    dd 0x3EB2F5B7
    dd 0xDCEA6F70
    dd 0x7B21E929
    dd 0x195962E2
    dd 0xB790DC9B
    dd 0x55C85654
    dd 0xF3FFD00D
    dd 0x923749C6
    dd 0x306EC37F
    dd 0xCEA63D38
    dd 0x6CDDB6F1
    dd 0x0B1530AA
    dd 0xA94CAA63
    dd 0x4784241C
    dd 0xE5BB9DD5
    dd 0x83F3178E
    dd 0x222A9147
    dd 0xC0620B00
    dd 0x5E9984B9
    dd 0xFCD0FE72
    dd 0x9B08782B
    dd 0x393FF1E4
    dd 0xD7776B9D
    dd 0x75AEE556
    dd 0x13E65F0F
    dd 0xB21DD8C8
    dd 0x50555281
    dd 0xEE8CCC3A
    dd 0x8CC445F3
    dd 0x2AFBBFAC
    dd 0xC9333965
    dd 0x676AB31E
    dd 0x05A22CD7
    dd 0xA3D9A690
    dd 0x42112049
    dd 0xE0489A02
    dd 0x7E8013BB
    dd 0x1CB78D74
    dd 0xBAEF072D
    dd 0x592680E6
    dd 0xF75DFA9F
    dd 0x95957458
    dd 0x33CCEE11
    dd 0xD20467CA
    dd 0x703BE183
    dd 0x0E735B3C
    dd 0xACAAD4F5
    dd 0x4AE24EAE
    dd 0xE919C867
    dd 0x87514220
    dd 0x2588BBD9
    dd 0xC3C03592
    dd 0x61F7AF4B
    dd 0x002F2904
    dd 0x9E66A2BD
    dd 0x3C9E1C76
    dd 0xDAD5962F
    dd 0x790D0FE8
    dd 0x174489A1
    dd 0xB57C035A
    dd 0x53B37D13
    dd 0xF1EAF6CC
    dd 0x90227085
    dd 0x2E59EA3E
    dd 0xCC9163F7
    dd 0x6AC8DDB0
    dd 0x09005769
    dd 0xA737D122
    dd 0x456F4ADB
    dd 0xE3A6C494
    dd 0x81DE3E4D
    dd 0x2015B806
    dd 0xBE4D31BF
    dd 0x5C84AB78
    dd 0xFABC2531
    dd 0x98F39EEA
    dd 0x372B18A3
    dd 0xD562925C
    dd 0x739A0C15
    dd 0x11D185CE
    dd 0xB008FF87
    dd 0x4E407940
    dd 0xEC77F2F9
    dd 0x8AAF6CB2
    dd 0x28E6E66B
    dd 0xC71E6024
    dd 0x6555D9DD
    dd 0x038D5396
    dd 0xA1C4CD4F
    dd 0x3FFC4708
    dd 0xDE33C0C1
    dd 0x7C6B3A7A
    dd 0x1AA2B433
    dd 0xB8DA2DEC
    dd 0x5711A7A5
    dd 0xF549215E
    dd 0x93809B17
    dd 0x31B814D0
    dd 0xCFEF8E89
    dd 0x6E270842
    dd 0x0C5E81FB
    dd 0xAA95FBB4
    dd 0x48CD756D
    dd 0xE704EF26
    dd 0x853C68DF
    dd 0x2373E298
    dd 0xC1AB5C51
    dd 0x5FE2D60A
    dd 0xFE1A4FC3
    dd 0x9C51C97C
    dd 0x3A894335
    dd 0xD8C0BCEE
    dd 0x76F836A7
    dd 0x152FB060
    dd 0xB3672A19
    dd 0x519EA3D2
    dd 0xEFD61D8B
    dd 0x8E0D9744
    dd 0x2C4510FD
    dd 0xCA7C8AB6
    dd 0x68B4046F
    dd 0x06EB7E28
    dd 0xA522F7E1
    dd 0x435A719A
    dd 0xE191EB53
    dd 0x7FC9650C
    dd 0x1E00DEC5
    dd 0xBC38587E
    dd 0x5A6FD237
    dd 0xF8A74BF0
    dd 0x96DEC5A9
    dd 0x35163F62
    dd 0xD34DB91B
    dd 0x718532D4
    dd 0x0FBCAC8D
    dd 0xADF42646
    dd 0x4C2B9FFF
    dd 0xEA6319B8
    dd 0x889A9371
    dd 0x26D20D2A
    dd 0xC50986E3
    dd 0x6341009C
    dd 0x01787A55
    dd 0x9FAFF40E
    dd 0x3DE76DC7
    dd 0xDC1EE780
    dd 0x7A566139
    dd 0x188DDAF2
    dd 0xB6C554AB
    dd 0x54FCCE64
    dd 0xF334481D
    dd 0x916BC1D6
    dd 0x2FA33B8F
    dd 0xCDDAB548
    dd 0x6C122F01
    dd 0x0A49A8BA
    dd 0xA8812273
    dd 0x46B89C2C
    dd 0xE4F015E5
    dd 0x83278F9E
    dd 0x215F0957
    dd 0xBF968310
    dd 0x5DCDFCC9
    dd 0xFC057682
    dd 0x9A3CF03B
    dd 0x387469F4
    dd 0xD6ABE3AD
    dd 0x74E35D66
    dd 0x131AD71F
    dd 0xB15250D8
    dd 0x4F89CA91
    dd 0xEDC1444A
    dd 0x8BF8BE03
    dd 0x2A3037BC
    dd 0xC867B175
    dd 0x669F2B2E
    dd 0x04D6A4E7
    dd 0xA30E1EA0
    dd 0x41459859
    dd 0xDF7D1212
    dd 0x7DB48BCB
    dd 0x1BEC0584
    dd 0xBA237F3D
    dd 0x585AF8F6
    dd 0xF69272AF
    dd 0x94C9EC68
    dd 0x33016621
    dd 0xD138DFDA
    dd 0x6F705993
    dd 0x0DA7D34C
    dd 0xABDF4D05
    dd 0x4A16C6BE
    dd 0xE84E4077
    dd 0x8685BA30
    dd 0x24BD33E9
    dd 0xC2F4ADA2
    dd 0x612C275B
    dd 0xFF63A114
    dd 0x9D9B1ACD
    dd 0x3BD29486
    dd 0xDA0A0E3F
    dd 0x784187F8
    dd 0x167901B1
    dd 0xB4B07B6A
    dd 0x52E7F523
    dd 0xF11F6EDC
    dd 0x8F56E895
    dd 0x2D8E624E
    dd 0xCBC5DC07
    dd 0x69FD55C0
    dd 0x0834CF79
    dd 0xA66C4932
    dd 0x44A3C2EB
    dd 0xE2DB3CA4
    dd 0x8112B65D
    dd 0x1F4A3016
    dd 0xBD81A9CF
vector_a_end:

vector_b:
    dd 0x00000000
    dd 0xFFFFFFFF
    dd 0x00000001
    dd 0x80000000
    dd 0x00000001
    dd 0x11111111
    dd 0x55555555
    dd 0x21524111
    dd 0xFFFFFFFF
    dd 0x7FFFFFFF
    dd 0xFFFF0001
    dd 0xFF000001
    dd 0x10203040
    dd 0x2468ACE0
    dd 0x40000000
    dd 0x35014542
    dd 0x00000001
    dd 0x392C67D8
    dd 0x87D0294D
    dd 0x0DC4F2F6
    dd 0x8BE8841B
    dd 0x119D498C
    dd 0x9F811331
    dd 0x65B5255A
    dd 0xE059EECF
    dd 0x6E4DB070
    dd 0xF47645E5
    dd 0x721A0F0E
    dd 0xF80ED0B3
    dd 0x46329A24
    dd 0xCC26AC49
    dd 0x4ACB71F2
    dd 0xD0FF3B67
    dd 0x5EE3CC88
    dd 0x2497963D
    dd 0xA2B85BA6
    dd 0x28AC6DCB
    dd 0xB750377C
    dd 0x3D44F8E1
    dd 0xBB68820A
    dd 0x011D57BF
    dd 0x8F011920
    dd 0x15352355
    dd 0x93D9F4FE
    dd 0x19CDBE63
    dd 0xE7F64394
    dd 0x6D9A1539
    dd 0xEB8EDEA2
    dd 0x71B2E0D7
    dd 0xFFA6AA78
    dd 0x7A4B7FED
    dd 0xC07F0116
    dd 0x4E63CABB
    dd 0xD4179C2C
    dd 0x523BA651
    dd 0xD82C6BFA
    dd 0xA6D03D6F
    dd 0x2CC4C690
    dd 0xAAE88805
    dd 0x309D5DAE
    dd 0xBE8167D3
    dd 0x04B52944
    dd 0x8359F2E9
    dd 0x094D8412
    dd 0x97764987
    dd 0x1D1A1328
    dd 0x9B0E255D
    dd 0x6132EEC6
    dd 0xEF26B06B
    dd 0x75CB459C
    dd 0xF3FF0F01
    dd 0x79E3D0AA
    dd 0xC7979ADF
    dd 0x4DBBAC40
    dd 0xCBAC71F5
    dd 0x56503B1E
    dd 0xDC44CC83
    dd 0x5A689634
    dd 0x201D5859
    dd 0xAE016DC2
    dd 0x34353777
    dd 0xB2D9F898
    dd 0x38CD820D
    dd 0x86F657B6
    dd 0x0C9A19DB
    dd 0x8A8E234C
    dd 0x10B2F4F1
    dd 0x9EA6BE1A
    dd 0x654B438F
    dd 0xE37F1530
    dd 0x6963DEA5
    dd 0xF717E0CE
    dd 0x7D3BAA73
    dd 0xFB2C7FE4
    dd 0x41D00109
    dd 0xCFC4CAB2
    dd 0x55E89C27
    dd 0xD39CA648
    dd 0x59816BFD
    dd 0x27B53D66
    dd 0xA259C68B
    dd 0x284D883C
    dd 0xB6765DA1
    dd 0x3C1A67CA
    dd 0xBA0E297F
    dd 0x0032F2E0
    dd 0x8E268415
    dd 0x14CB49BE
    dd 0x92FF1323
    dd 0x18E32554
    dd 0xE697EEF9
    dd 0x6CBBB062
    dd 0xEAAC4597
    dd 0x71500F38
    dd 0xFF44D0AD
    dd 0x45689AD6
    dd 0xC31CAC7B
    dd 0x490171EC
    dd 0xD7353B11
    dd 0x5DD9CCBA
    dd 0xDBCD962F
    dd 0xA1F65850
    dd 0x2F9A6DC5
    dd 0xB58E376E
    dd 0x33B2F893
    dd 0xB9A68204
    dd 0x044B57A9
    dd 0x827F19D2
    dd 0x08632347
    dd 0x9617F4E8
    dd 0x1C3BBE1D
    dd 0x9A2C4386
    dd 0x60D0152B
    dd 0xEEC4DF5C
    dd 0x74E8E0C1
    dd 0xF29CAA6A
    dd 0x78817F9F
    dd 0xC6B50100
    dd 0x4D59CAB5
    dd 0xCB4D9CDE
    dd 0x5171A643
    dd 0xDF1A6BF4
    dd 0xA50E3D19
    dd 0x2332C682
    dd 0xA9268837
    dd 0x37CB5258
    dd 0xBDFF67CD
    dd 0x3BE32976
    dd 0x8197F29B
    dd 0x0FBB840C
    dd 0x95AC49B1
    dd 0x105013DA
    dd 0x9E44254F
    dd 0x6468EEF0
    dd 0xE21CB065
    dd 0x6801458E
    dd 0xF6350F33
    dd 0x7CD9D0A4
    dd 0xFACD9AC9
    dd 0x40F1AC72
    dd 0xCE9A71E7
    dd 0x548E3B08
    dd 0xD2B2CCBD
    dd 0x58A69626
    dd 0x274B584B
    dd 0xAD7F6DFC
    dd 0x2B633761
    dd 0xB117F88A
    dd 0x3F3B823F
    dd 0x852C57A0
    dd 0x03D019D5
    dd 0x89C4237E
    dd 0x17E8F4E3
    dd 0x9D9CBE14
    dd 0x1B8143B9
    dd 0xE1B51522
    dd 0x6C59DF57
    dd 0xEA4DE0F8
    dd 0x7071AA6D
    dd 0xFE1A7F96
    dd 0x440E013B
    dd 0xC232CAAC
    dd 0x48269CD1
    dd 0xD6CAA67A
    dd 0x5CFF6BEF
    dd 0xDAE33D10
    dd 0xA097C685
    dd 0x2EBB882E
    dd 0xB4AC5253
    dd 0x335067C4
    dd 0xB9442969
    dd 0x0768F292
    dd 0x8D1C8407
    dd 0x0B0149A8
    dd 0x913513DD
    dd 0x1FD92546
    dd 0xE5CDEEEB
    dd 0x63F1B01C
    dd 0xE99A4581
    dd 0x778E0F2A
    dd 0xFDB2D15F
    dd 0x7BA69AC0
    dd 0xC64AAC75
    dd 0x4C7F719E
    dd 0xCA633B03
    dd 0x5017CCB4
    dd 0xDE3B96D9
    dd 0xA42C5842
    dd 0x22D06DF7
    dd 0xA8C43718
    dd 0x36E8F88D
    dd 0xBC9C8236
    dd 0x3A81545B
    dd 0x80B519CC
    dd 0x0F592371
    dd 0x954DF49A
    dd 0x1371BE0F
    dd 0x991A43B0
    dd 0x670E1525
    dd 0xED32DF4E
    dd 0x6B26E0F3
    dd 0xF1CAAA64
    dd 0x7FFF7F89
    dd 0xC5E30132
    dd 0x4397CAA7
    dd 0xC9BB9CC8
    dd 0x57AFA67D
    dd 0xD2506BE6
    dd 0x58443D0B
    dd 0x2668C6BC
    dd 0xAC1C8821
    dd 0x2A01524A
    dd 0xB03567FF
    dd 0x3ED92960
    dd 0x84CDF295
    dd 0x02F1843E
    dd 0x889A49A3
    dd 0x168E13D4
    dd 0x9CB22579
    dd 0x1AA6EEE2
    dd 0xE14AB017
    dd 0x6F7F45B8
    dd 0xF5630F2D
    dd 0x7317D156
    dd 0xF93B9AFB
    dd 0x472FAC6C
    dd 0xCDD07191
    dd 0x4BC43B3A
    dd 0xD1E8CCAF
    dd 0x5F9C96D0
    dd 0x25815845
    dd 0xA3B56DEE
    dd 0x2E593713
    dd 0xB44DF884
    dd 0x32718229
    dd 0xB81A5452
vector_b_end:

vector_g:
    dd 0x00000000
    dd 0x00000000
    dd 0x00000000
    dd 0x00000000
    dd 0x80000000
    dd 0x23456789
    dd 0xFFFFFFFF
    dd 0x00000000
    dd 0xFFFFFFFE
    dd 0xFFFFFFFF
    dd 0x00000000
    dd 0x00000000
    dd 0x11223344
    dd 0x37C048BF
    dd 0x80000000
    dd 0x00000000
    dd 0x00000002
    dd 0xDF1AE7A9
    dd 0xCBF622D7
    dd 0xF0226639
    dd 0x0C7D7117
    dd 0x3069B041
    dd 0x5C84F39F
    dd 0xC0F07F81
    dd 0xD9CCC2AF
    dd 0x05F7FE09
    dd 0x2A580D37
    dd 0x46335019
    dd 0x6A5F8B77
    dd 0x56BACEA1
    dd 0x7AE65A7F
    dd 0x97C299E1
    dd 0xBC2DDD0F
    dd 0xE849E7E9
    dd 0x4C352B57
    dd 0x688D6A79
    dd 0x8CB8F657
    dd 0xB99439C1
    dd 0xDDC074DF
    dd 0xFA1B77C1
    dd 0xDE07C72F
    dd 0x0A230249
    dd 0x2E8E8637
    dd 0x4B6AD199
    dd 0x6F9614B7
    dd 0xDBF613A1
    dd 0xFFD15EFF
    dd 0x1BFDA221
    dd 0x40591E0F
    dd 0x6C846169
    dd 0x8560B097
    dd 0x69CBAB79
    dd 0x95E7EED7
    dd 0xB9D33A01
    dd 0xD62EBDDF
    dd 0xFA56FD41
    dd 0x6732486F
    dd 0x8B5E4B49
    dd 0xA7B98677
    dd 0xCBA5D5D9
    dd 0xF7C159B7
    dd 0xDC2C94E1
    dd 0xF908D83F
    dd 0x1D33E321
    dd 0x4994224F
    dd 0x6D6F65A9
    dd 0x899AF197
    dd 0xEDF734B9
    dd 0x1A227017
    dd 0x3EFE7F01
    dd 0x5B69C21F
    dd 0x7F85FD81
    dd 0x6B71416F
    dd 0x8FCCCC89
    dd 0xABF50BF7
    dd 0xD4D04ED9
    dd 0xF8FC59F7
    dd 0x15579D61
    dd 0x7943D93F
    dd 0xA55F6861
    dd 0xC9CAABCF
    dd 0xE6A6E6A9
    dd 0x0AD1E9D7
    dd 0xF7323939
    dd 0x1B0D7517
    dd 0x3738F841
    dd 0x5B95439F
    dd 0x87C08681
    dd 0xEC9C85AF
    dd 0x0907D109
    dd 0x2D241437
    dd 0x590F9019
    dd 0x7D6AD377
    dd 0x999322A1
    dd 0x7E6E1D7F
    dd 0xAA9A60E1
    dd 0xCEF5AC0F
    dd 0xEAE12FE9
    dd 0x0EFD6F57
    dd 0x7B68BA79
    dd 0x9444BD57
    dd 0xB86FF8C1
    dd 0xE4D047DF
    dd 0x08ABCBC1
    dd 0x24D7072F
    dd 0x09334A49
    dd 0x355E5537
    dd 0x5A3A9499
    dd 0x76A5D7B7
    dd 0x9AC163A1
    dd 0x06ADA6FF
    dd 0x2B08E221
    dd 0x4730F10F
    dd 0x6C0C3469
    dd 0x98386F97
    dd 0x7C93B379
    dd 0x987F3ED7
    dd 0xBC9B7E01
    dd 0xE906C0DF
    dd 0x0DE2CC41
    dd 0x2A0E0F6F
    dd 0x8E6E4B49
    dd 0xBA49DA77
    dd 0xDE751DD9
    dd 0xFAD158B7
    dd 0x1EFC5BE1
    dd 0x07D8AB3F
    dd 0x2443E721
    dd 0x485F6A4F
    dd 0x744BB5A9
    dd 0x98A6F897
    dd 0xB4CEF7B9
    dd 0x19AA4317
    dd 0x45D68701
    dd 0x6A32021F
    dd 0x861D4581
    dd 0xAA39946F
    dd 0x96A48F89
    dd 0xBB80D2F7
    dd 0xD7AC1ED9
    dd 0xFC07A1F7
    dd 0x27E7E161
    dd 0x8C132C3F
    dd 0xA86F2F61
    dd 0xCC9A6ACF
    dd 0xF976AEA9
    dd 0x1DE23DD7
    dd 0x39FD7939
    dd 0x1DE9BC17
    dd 0x4A44C741
    dd 0x6E6D069F
    dd 0x87484A81
    dd 0xB373D5AF
    dd 0x17D01909
    dd 0x33BB5437
    dd 0x57D76319
    dd 0x8442A677
    dd 0xA91EE1A1
    dd 0xC54A257F
    dd 0xA9A5B0E1
    dd 0xD585F00F
    dd 0xF9B132E9
    dd 0x160D3E57
    dd 0x3A388179
    dd 0xA714BD57
    dd 0xCB804CC1
    dd 0xE79B8FDF
    dd 0x0B87CAC1
    dd 0x37E2CE2F
    dd 0x1C0B1D49
    dd 0x38E65937
    dd 0x5D11DC99
    dd 0x896E27B7
    dd 0xAD596AA1
    dd 0xC97569FF
    dd 0x2DE0B521
    dd 0x56BCF90F
    dd 0x72E87469
    dd 0x9743B797
    dd 0xC3240679
    dd 0xA74F01D7
    dd 0xC3AB4501
    dd 0xE7D690DF
    dd 0x14B21441
    dd 0x391E536F
    dd 0x55399E49
    dd 0xB925A177
    dd 0xE580DCD9
    dd 0x09A920B7
    dd 0x2684AFE1
    dd 0x4AAFEB3F
    dd 0x370C2E21
    dd 0x5AF7394F
    dd 0x771378A9
    dd 0x9B7EBC97
    dd 0xC85A47B9
    dd 0x2C868B17
    dd 0x48E1C601
    dd 0x6CC1D51F
    dd 0x98ED1881
    dd 0xBD49546F
    dd 0xD9749789
    dd 0xC25022F7
    dd 0xE6BC61D9
    dd 0x02D7A4F7
    dd 0x26C3B061
    dd 0x531EF43F
    dd 0xB7472F61
    dd 0xD422BECF
    dd 0xF84E01A9
    dd 0x24AA3CD7
    dd 0x48954039
    dd 0x64B18C17
    dd 0x491CCB41
    dd 0x75F84E9F
    dd 0x9A249981
    dd 0xB67FDCAF
    dd 0xDA5FDC09
    dd 0x468B2737
    dd 0x6AE76B19
    dd 0x8712E677
    dd 0xABEE29A1
    dd 0xD85A787F
    dd 0xBC7573E1
    dd 0xD861B70F
    dd 0xFCBD02E9
    dd 0x28E88657
    dd 0x41C0C579
    dd 0x65EC1057
    dd 0xD24813C1
    dd 0xF6334EDF
    dd 0x124F92C1
    dd 0x36BB222F
    dd 0x63965D49
    dd 0x47C2A037
    dd 0x641DAB99
    dd 0x87FDEAB7
    dd 0xB4292EA1
    dd 0xD884B9FF
    dd 0xF4B0FD21
    dd 0x598C380F
    dd 0x85F84769
    dd 0xAA138A97
    dd 0xC5FFC679
    dd 0xEA5B09D7
    dd 0xD6869501
    dd 0xFB5ED3DF
    dd 0x178A1741
    dd 0x3BE6226F
    dd 0x67D16649
    dd 0xCBEDA177
    dd 0xE85930D9
    dd 0x113473B7
    dd 0x3560AEE1
    dd 0x51BBB23F
    dd 0x759BFE21
vector_g_end:

case_counts:
    dw 0,1,15,16,17,255,256

msg_ok:
    dw 'N','G','_','N','O','V','A','_','C','P','U','_','R','1','_','O','K',' '
    dw 'C','A','S','E','S','=','7',' ','N','=','0',',','1',',','1','5',',','1','6',','
    dw '1','7',',','2','5','5',',','2','5','6',' ','S','E','M','=','M','O','D','2','P','3','2',' '
    dw 'E','X','E','C','=','S','C','A','L','A','R',' ','D','I','G','E','S','T','=','7','C','F','3','0','2','6','8','5','8','C','7','A','C','7','C',13,10,0
msg_e201:
    dw 'N','G','_','N','O','V','A','_','C','P','U','_','R','1','_','E','2','0','1','_','R','E','S','U','L','T',13,10,0
msg_e202:
    dw 'N','G','_','N','O','V','A','_','C','P','U','_','R','1','_','E','2','0','2','_','G','U','A','R','D',13,10,0
msg_e203:
    dw 'N','G','_','N','O','V','A','_','C','P','U','_','R','1','_','E','2','0','3','_','S','U','F','F','I','X',13,10,0
msg_e204:
    dw 'N','G','_','N','O','V','A','_','C','P','U','_','R','1','_','E','2','0','4','_','C','O','R','P','U','S',13,10,0
msg_e205:
    dw 'N','G','_','N','O','V','A','_','C','P','U','_','R','1','_','E','2','0','5','_','D','I','G','E','S','T',13,10,0

bootstrap_external_asm_tag:
    db 'BOOTSTRAP_EXTERNAL_ASM',0
nova_compute_external_probe_tag:
    db 'NOVA_COMPUTE_R1_EXTERNAL_PROBE',0
non_release_tag:
    db 'NON_RELEASE_UEFI_PREKERNEL_PROBE',0
    align 8, db 0
reloc_anchor:
    dq IMAGE_BASE + entry
rdata_end:
    %if (vector_a_end - vector_a) != 1024
        %error "NC-R1 A table is not 256 dwords"
    %endif
    %if (vector_b_end - vector_b) != 1024
        %error "NC-R1 B table is not 256 dwords"
    %endif
    %if (vector_g_end - vector_g) != 1024
        %error "NC-R1 G table is not 256 dwords"
    %endif
    %if (rdata_end - rdata_start) > 0x1000
        %error "NC-R1 .rdata exceeds 0x1000"
    %endif
    times 0x1000 - ($ - $$) db 0

SECTION .data start=0x1800 vstart=0x3000 align=16
data_start:
guard_pre:
    dd GUARD_PRE_VALUE
out_vec:
    times VECTOR_CAPACITY dd OUTPUT_SENTINEL
guard_post:
    dd GUARD_POST_VALUE
run_digest:
    dq FNV_OFFSET_BASIS
stream_size:
    dd 0
current_n:
    dw 0
    dw 0
case_index:
    dd 0
data_end:
    %if (data_end - data_start) > 0x0600
        %error "NC-R1 .data exceeds 0x600"
    %endif
    times 0x0600 - ($ - $$) db 0

SECTION .reloc start=0x1E00 vstart=0x4000 align=4
reloc_start:
    dd 0x2000
    dd 12
    dw 0xA000 | (reloc_anchor - rdata_start)
    dw 0
reloc_end:
    times 0x0200 - ($ - $$) db 0
