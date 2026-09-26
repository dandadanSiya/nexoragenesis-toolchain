// Nexora Kart memory map. One 8 MiB block is obtained from UEFI
// AllocatePages before ExitBootServices; every offset below is relative to
// its base, which the kernel keeps in g_state.

%define STATE_PAGES 2048
%define STATE_BYTES STATE_PAGES * 4096
%define STATE_WORDS STATE_BYTES >> 3

// Kernel slots (u64)
%define S_FB 0
%define S_STRIDE 8
%define S_WIDTH 16
%define S_HEIGHT 24
%define S_FORMAT 32
%define S_SCALE 40
%define S_ROWBYTES 48
%define S_ORIGIN 56
%define S_TSC_MS 64
%define S_TSC0 72
%define S_E0 80
%define S_MODE_FOUND 88
%define S_SEED 96

// Keys: held flags then "pressed since last read" flags, one byte each.
%define KEYS 128
%define KEYS_HIT 136
%define K_UP 0
%define K_DOWN 1
%define K_LEFT 2
%define K_RIGHT 3
%define K_SPACE 4
%define K_ENTER 5
%define K_ESC 6
%define K_DRIFT 7

// Game slots (u64)
%define G_SCENE 256
%define G_NOW 264
%define G_LAST 272
%define G_ACC 280
%define G_TICKS 288
%define G_RACE_TICKS 296
%define G_COUNT 304
%define G_CAM_X 312
%define G_CAM_Y 320
%define G_CAM_A 328
%define G_PLACE 336
%define G_FINISH_TICKS 344
%define G_TONE_TICKS 352
%define G_FRAMES 360
%define G_HOP 368
%define G_BEST 376
%define G_FIN_PLACE 384
%define G_ORDER 392

// Karts: 4 records of 16 u64 slots.
%define KARTS 1024
%define KART_SIZE 128
%define KX 0
%define KY 8
%define KA 16
%define KV 24
%define KWP 32
%define KLAP 40
%define KPROG 48
%define KSTEER 56
%define KMAX 64
%define KSKIN 72
%define KDONE 80
%define KTIME 88
%define KDIST 96
%define KBOOST 104
%define KDEPTH 112
%define KSCREEN 120

// Sprite draw list: 4 x (depth, kart index)
%define DRAW 1664

// Palette lookup: 256 x u32 in framebuffer pixel order.
%define LUT 2048
// Back buffer: 320 x 200 palette indices.
%define BACK 4096
%define BACK_W 320
%define BACK_H 200
// Scratch for the UEFI memory map.
%define MEMMAP 131072
%define MEMMAP_BYTES 65536
