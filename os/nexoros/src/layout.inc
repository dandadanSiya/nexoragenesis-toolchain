// Nexora OS — memory map and constants.
//
// The kernel runs on its own page tables: an identity map of the first
// 4 GiB with 2 MiB pages, except one 2 MiB window mapped with 4 KiB pages
// that holds the task stacks, each with an unmapped guard page below it.
// Everything the kernel owns lives in one block obtained from UEFI
// AllocatePages before ExitBootServices; g_base holds its address.

%define PAGE 4096
%define MB 1048576
%define BLOCK_PAGES 16384        // 64 MiB

// ---- Offsets inside the block ----
%define OFF_STATE 0              // 16 KiB of kernel globals
%define OFF_GDT 16384            // 8 descriptors
%define OFF_GDTR 16448           // 10-byte pseudo-descriptor
%define OFF_IDTR 16464
%define OFF_IDT 17408            // 48 gates x 16 bytes
%define OFF_PML4 20480
%define OFF_PDPT 24576
%define OFF_PD 28672             // 4 pages, one per GiB
%define OFF_STACK_PT 45056       // page table of the stack window
%define OFF_BITMAP 49152         // 8 pages: 1 bit per 4 KiB frame (1 GiB)
%define BITMAP_BYTES 32768
%define OFF_TASKS 81920          // 16 tasks x 512 bytes
%define OFF_MMAP 90112           // saved UEFI memory map (64 KiB)
%define MMAP_BYTES 65536
%define OFF_LUT 155648           // 256 x u32 pixel values
%define OFF_HEAP 163840          // kernel heap (bump), 48 MiB
%define HEAP_END 50495488
%define OFF_STACK_MIN 54525952   // stack window: first 2 MiB boundary after this

// ---- Kernel globals (offsets from the state base) ----
%define S_BASE 0
%define S_BS 8
%define S_FB 16
%define S_FB_W 24
%define S_FB_H 32
%define S_FB_STRIDE 40           // bytes per scanline
%define S_FB_FORMAT 48
%define S_TSC_MS 56              // TSC ticks per millisecond
%define S_TICKS 64               // milliseconds since interrupts started
%define S_CUR 72                 // running task index
%define S_HEAP 80                // next free heap offset
%define S_FRAMES_TOTAL 88
%define S_FRAMES_FREE 96
%define S_IMAGE 104
%define S_MMAP_SIZE 112
%define S_MMAP_DESC 120
%define S_STACK_WIN 128          // physical base of the 2 MiB stack window
%define S_SWITCHES 136           // context switches performed
%define S_TONE 144               // PC speaker milliseconds left
%define S_LAYOUT 152             // 0 AZERTY, 1 QWERTY
%define S_RAM_KB 160             // RAM reported by the firmware
%define S_ORIGIN_X 168
%define S_ORIGIN_Y 176
%define S_PANIC 184
%define S_IRQ_KBD 192            // keyboard interrupts seen

// Console
%define CON_COLS 80
%define CON_ROWS 30
%define CON_CELLS 2400
%define S_CON_X 256
%define S_CON_Y 264
%define S_CON_COLOR 272
%define S_CON_TOP 280            // first scrolling row (row 0 is the bar)
%define CON_CHARS 1024           // 2400 bytes
%define CON_COLORS 4096          // 2400 bytes

// Keyboard
%define KEY_HELD 7168            // 128 bytes: scancode set 1 key down
%define KEY_RING 7424            // 256-byte ring of typed characters
%define S_KEY_HEAD 7680
%define S_KEY_TAIL 7688
%define S_KEY_E0 7696
%define S_KEY_SHIFT 7704

// Spill slots, one group per function (every call clobbers every register)
%define SV_A 8192
%define SV_B 8256
%define SV_C 8320
%define SV_D 8384
%define SV_E 8448
%define SV_F 8512
%define SV_G 8576
%define SV_H 8640
%define SV_I 8704
%define SV_J 8768
%define SV_K 8832
%define SV_L 8896

// ---- Tasks ----
%define MAX_TASKS 16
%define T_SIZE 512
%define T_STATE 0
%define T_FRAME 8                // saved interrupt frame (on the task stack)
%define T_WAKE 16                // wake-up time in ms
%define T_KIND 24                // 0 kernel thread, 1 Nova process
%define T_ENTRY 32
%define T_ARG 40
%define T_IMAGE 48
%define T_CONTEXT 56
%define T_CPU 64                 // timer ticks spent running
%define T_START 72
%define T_EXIT 80
%define T_NAME 96                // 32 bytes
%define T_NOVA 128               // Nova runtime state (see nova.ntasm)

%define TS_FREE 0
%define TS_READY 1
%define TS_SLEEP 2
%define TS_WAIT_KEY 3
%define TS_DEAD 4

// Stack window: task i uses pages [i*32+1, i*32+31], page i*32 is a guard.
%define STACK_SLOT 131072        // 32 pages
%define STACK_TOP_OFF 131072

// ---- Interrupt frame built by the dispatch stub ----
%define F_R15 0
%define F_R14 8
%define F_R13 16
%define F_R12 24
%define F_R11 32
%define F_R10 40
%define F_R9 48
%define F_R8 56
%define F_RDI 64
%define F_RSI 72
%define F_RBP 80
%define F_RBX 88
%define F_RDX 96
%define F_RCX 104
%define F_RAX 112
%define F_VECTOR 120
%define F_ERROR 128
%define F_RIP 136
%define F_CS 144
%define F_RFLAGS 152
%define F_RSP 160
%define F_SS 168
%define F_BYTES 176

// Selectors (same slots as OVMF so CS never needs a far reload)
%define SEL_DATA 48
%define SEL_CODE 56

// Console colours (see tools/gen_assets.py)
%define C_BG 0
%define C_FG 1
%define C_DIM 2
%define C_ACCENT 3
%define C_GOOD 4
%define C_WARN 5
%define C_BAD 6
%define C_BLUE 7
%define C_PURPLE 8
%define C_CYAN 9
%define C_PANEL 10

// PIC / PIT ports
%define PIC1_CMD 32
%define PIC1_DATA 33
%define PIC2_CMD 160
%define PIC2_DATA 161
%define PIT_CH0 64
%define PIT_CMD 67
