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
%define G_FX 360
%define G_FY 368
%define G_BEST 376
%define G_FIN_PLACE 384
%define G_GO 392
%define G_BANNER 400
%define G_STEPS 408

// Text line being composed (TEXT_LEN bytes at TEXT).
%define TEXT_I 496
%define TEXT_LEN 504
%define TEXT 512
%define DIGITS 576

// Karts: 4 records of 32 u64 slots.
%define KARTS 1024
%define KART_SIZE 256
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
%define KBASE 96
%define KBOOST 104
%define KDEPTH 112
%define KSX 120
%define KSY 128
%define KSIZE 136
%define KSTEP 144
%define KPLACE 152

// Palette lookup: 256 x u32 in framebuffer pixel order.
%define LUT 3072
// Back buffer: 320 x 200 palette indices.
%define BACK 4096
%define BACK_W 320
%define BACK_H 200
// Scratch for the UEFI memory map.
%define MEMMAP 131072
%define MEMMAP_BYTES 65536

// Scenes
%define SCENE_TITLE 0
%define SCENE_COUNTDOWN 1
%define SCENE_RACE 2
%define SCENE_FINISH 3

// Driving model, per 1/60 s step, positions in 16.16 world units
%define MAXV 100000
%define ACCEL 900
%define BRAKE 2400
%define FRICT 500
%define TURN 22
%define CAM_BACK 30
%define LAPS 3
%define CENTER_TEXT 1000

// Palette indices (see tools/gen_assets.py)
%define C_WHITE 1
%define C_INK 2
%define C_YELLOW 3
%define C_RED 4
%define C_BLUE 6
%define C_GREEN 8
%define C_LIGHT 10
%define C_ORANGE 11
%define GRASS_COLOR 32
%define SKY_COLOR 80
%define MOUNT_FAR_COLOR 96
%define MOUNT_NEAR_COLOR 97
%define SNOW_COLOR 98
%define LAPS_DONE 4
%define G_DRAWN 416
%define MINI_X 250
%define MINI_Y 4
%define STEP_MUL 1092
%define NEAR_Z 196608
%define PASS_RADIUS 23040
%define WORLD_MIN 1048576
%define WORLD_MAX 32505856
%define WORLD_MID 16777216
%define BAND 12288
%define COLLIDE_R2 5308416
%define FAR_DIST 1048320
// Per-function spill slots: every call may change every register, so values
// that must survive a call live here.
%define SV_TD_X 2048
%define SV_TD_Y 2056
%define SV_TD_C 2064
%define SV_TT_MIN 2072
%define SV_TT_SEC 2080
%define SV_TT_CS 2088
%define SV_TP 2096
%define SV_HC 2104
%define SV_DA 2112
%define SV_RC 2120
%define SV_MM 2128
