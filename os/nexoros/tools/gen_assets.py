#!/usr/bin/env python3
"""Generates assets.ntasm for Nexora OS: the console palette and an 8x16
bitmap font. All art is original and procedural."""
import sys
from pathlib import Path

# Console palette: 16 entries, index 0 is the background.
PALETTE = [
    (12, 14, 22),      # 0 background (deep blue-grey)
    (220, 226, 232),   # 1 foreground (near white)
    (120, 130, 150),   # 2 dim
    (250, 210, 80),    # 3 accent (yellow)
    (90, 210, 120),    # 4 good (green)
    (240, 170, 60),    # 5 warn (orange)
    (235, 90, 90),     # 6 bad (red)
    (90, 160, 250),    # 7 blue
    (180, 120, 240),   # 8 purple
    (80, 220, 220),    # 9 cyan
    (40, 46, 66),      # 10 panel
    (26, 30, 46),      # 11 panel dark
    (60, 68, 92),      # 12 border
    (150, 160, 180),   # 13 mid
    (200, 120, 160),   # 14 pink
    (250, 250, 250),   # 15 pure white
]
while len(PALETTE) < 256:
    PALETTE.append((0, 0, 0))

# 5x7 source glyphs (rows top to bottom, "1" = ink). Rendered into an 8x16
# cell: 1px left margin, each source pixel becomes a 1x2 block from row 1, so
# the type is 5 wide and 14 tall with a baseline gap.
G = {
    " ": [], "!": ["001", "001", "001", "001", "001", "000", "001"],
    '"': ["101", "101", "000", "000", "000", "000", "000"],
    "#": ["01010", "11111", "01010", "01010", "11111", "01010", "00000"],
    "$": ["00100", "01111", "10100", "01110", "00101", "11110", "00100"],
    "%": ["11001", "11010", "00100", "01011", "10011", "00000", "00000"],
    "&": ["01100", "10010", "01100", "10011", "10010", "01101", "00000"],
    "'": ["001", "001", "010", "000", "000", "000", "000"],
    "(": ["001", "010", "100", "100", "100", "010", "001"],
    ")": ["100", "010", "001", "001", "001", "010", "100"],
    "*": ["00000", "10101", "01110", "11111", "01110", "10101", "00000"],
    "+": ["00000", "00100", "00100", "11111", "00100", "00100", "00000"],
    ",": ["000", "000", "000", "000", "001", "001", "010"],
    "-": ["00000", "00000", "00000", "11111", "00000", "00000", "00000"],
    ".": ["000", "000", "000", "000", "000", "011", "011"],
    "/": ["00001", "00010", "00100", "01000", "10000", "00000", "00000"],
    "0": ["01110", "10011", "10101", "10101", "11001", "01110", "00000"],
    "1": ["00100", "01100", "00100", "00100", "00100", "01110", "00000"],
    "2": ["01110", "10001", "00010", "00100", "01000", "11111", "00000"],
    "3": ["11110", "00001", "01110", "00001", "00001", "11110", "00000"],
    "4": ["00110", "01010", "10010", "11111", "00010", "00010", "00000"],
    "5": ["11111", "10000", "11110", "00001", "00001", "11110", "00000"],
    "6": ["00110", "01000", "11110", "10001", "10001", "01110", "00000"],
    "7": ["11111", "00001", "00010", "00100", "01000", "01000", "00000"],
    "8": ["01110", "10001", "01110", "10001", "10001", "01110", "00000"],
    "9": ["01110", "10001", "10001", "01111", "00010", "01100", "00000"],
    ":": ["000", "011", "011", "000", "011", "011", "000"],
    ";": ["000", "011", "011", "000", "001", "001", "010"],
    "<": ["00010", "00100", "01000", "10000", "01000", "00100", "00010"],
    "=": ["00000", "00000", "11111", "00000", "11111", "00000", "00000"],
    ">": ["01000", "00100", "00010", "00001", "00010", "00100", "01000"],
    "?": ["01110", "10001", "00010", "00100", "00000", "00100", "00000"],
    "@": ["01110", "10001", "10111", "10101", "10111", "01110", "00000"],
    "[": ["011", "010", "010", "010", "010", "010", "011"],
    "\\": ["10000", "01000", "00100", "00010", "00001", "00000", "00000"],
    "]": ["110", "010", "010", "010", "010", "010", "110"],
    "^": ["00100", "01010", "10001", "00000", "00000", "00000", "00000"],
    "_": ["00000", "00000", "00000", "00000", "00000", "00000", "11111"],
    "`": ["010", "001", "000", "000", "000", "000", "000"],
    "{": ["001", "010", "010", "100", "010", "010", "001"],
    "|": ["1", "1", "1", "1", "1", "1", "1"],
    "}": ["100", "010", "010", "001", "010", "010", "100"],
    "~": ["00000", "01000", "10101", "00010", "00000", "00000", "00000"],
}
for c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ":
    G[c] = None  # filled below
UPPER = {
    "A": ["01110", "10001", "10001", "11111", "10001", "10001", "00000"],
    "B": ["11110", "10001", "11110", "10001", "10001", "11110", "00000"],
    "C": ["01110", "10001", "10000", "10000", "10001", "01110", "00000"],
    "D": ["11100", "10010", "10001", "10001", "10010", "11100", "00000"],
    "E": ["11111", "10000", "11110", "10000", "10000", "11111", "00000"],
    "F": ["11111", "10000", "11110", "10000", "10000", "10000", "00000"],
    "G": ["01110", "10001", "10000", "10111", "10001", "01111", "00000"],
    "H": ["10001", "10001", "11111", "10001", "10001", "10001", "00000"],
    "I": ["01110", "00100", "00100", "00100", "00100", "01110", "00000"],
    "J": ["00111", "00010", "00010", "00010", "10010", "01100", "00000"],
    "K": ["10001", "10010", "11100", "10010", "10001", "10001", "00000"],
    "L": ["10000", "10000", "10000", "10000", "10000", "11111", "00000"],
    "M": ["10001", "11011", "10101", "10101", "10001", "10001", "00000"],
    "N": ["10001", "11001", "10101", "10011", "10001", "10001", "00000"],
    "O": ["01110", "10001", "10001", "10001", "10001", "01110", "00000"],
    "P": ["11110", "10001", "11110", "10000", "10000", "10000", "00000"],
    "Q": ["01110", "10001", "10001", "10101", "10010", "01101", "00000"],
    "R": ["11110", "10001", "11110", "10010", "10001", "10001", "00000"],
    "S": ["01111", "10000", "01110", "00001", "00001", "11110", "00000"],
    "T": ["11111", "00100", "00100", "00100", "00100", "00100", "00000"],
    "U": ["10001", "10001", "10001", "10001", "10001", "01110", "00000"],
    "V": ["10001", "10001", "10001", "10001", "01010", "00100", "00000"],
    "W": ["10001", "10001", "10101", "10101", "10101", "01010", "00000"],
    "X": ["10001", "01010", "00100", "00100", "01010", "10001", "00000"],
    "Y": ["10001", "01010", "00100", "00100", "00100", "00100", "00000"],
    "Z": ["11111", "00010", "00100", "01000", "10000", "11111", "00000"],
}
G.update(UPPER)
# Lowercase reuses uppercase shapes for this pixel console.
for c in "abcdefghijklmnopqrstuvwxyz":
    G[c] = UPPER[c.upper()]


def glyph_cell(rows):
    """Render a 5x7 (or narrower) glyph into 16 bytes (8 wide x 16 tall)."""
    cell = [0] * 16
    for r, bits in enumerate(rows or []):
        for x, ch in enumerate(bits):
            if ch == "1":
                for dy in range(2):
                    y = 1 + r * 2 + dy
                    if y < 16:
                        cell[y] |= 1 << (7 - (1 + x))
    return bytes(cell)


def font():
    out = bytearray()
    for code in range(32, 127):
        out += glyph_cell(G.get(chr(code), []))
    return bytes(out)


# Scan code set 1 -> character. The console font only draws ASCII, so the
# accented AZERTY keys produce their base letter.
AZERTY = {
    0x02: "&1", 0x03: "e2", 0x04: '"3', 0x05: "'4", 0x06: "(5", 0x07: "-6",
    0x08: "e7", 0x09: "_8", 0x0A: "c9", 0x0B: "a0", 0x0C: ")o", 0x0D: "=+",
    0x10: "aA", 0x11: "zZ", 0x12: "eE", 0x13: "rR", 0x14: "tT", 0x15: "yY",
    0x16: "uU", 0x17: "iI", 0x18: "oO", 0x19: "pP", 0x1A: "^^", 0x1B: "$*",
    0x1E: "qQ", 0x1F: "sS", 0x20: "dD", 0x21: "fF", 0x22: "gG", 0x23: "hH",
    0x24: "jJ", 0x25: "kK", 0x26: "lL", 0x27: "mM", 0x28: "u%", 0x2B: "*u",
    0x2C: "wW", 0x2D: "xX", 0x2E: "cC", 0x2F: "vV", 0x30: "bB", 0x31: "nN",
    0x32: ",?", 0x33: ";.", 0x34: ":/", 0x35: "!!", 0x56: "<>", 0x39: "  ",
}
QWERTY = {
    0x02: "1!", 0x03: "2@", 0x04: "3#", 0x05: "4$", 0x06: "5%", 0x07: "6^",
    0x08: "7&", 0x09: "8*", 0x0A: "9(", 0x0B: "0)", 0x0C: "-_", 0x0D: "=+",
    0x10: "qQ", 0x11: "wW", 0x12: "eE", 0x13: "rR", 0x14: "tT", 0x15: "yY",
    0x16: "uU", 0x17: "iI", 0x18: "oO", 0x19: "pP", 0x1A: "[{", 0x1B: "]}",
    0x1E: "aA", 0x1F: "sS", 0x20: "dD", 0x21: "fF", 0x22: "gG", 0x23: "hH",
    0x24: "jJ", 0x25: "kK", 0x26: "lL", 0x27: ";:", 0x28: "'\"", 0x29: "`~",
    0x2B: "\\|", 0x2C: "zZ", 0x2D: "xX", 0x2E: "cC", 0x2F: "vV", 0x30: "bB",
    0x31: "nN", 0x32: "mM", 0x33: ",<", 0x34: ".>", 0x35: "/?", 0x39: "  ",
}
# Keys shared by both layouts: Enter, Backspace, Tab, keypad digits.
COMMON = {0x1C: "\n", 0x0E: "\b", 0x0F: "\t", 0x47: "7", 0x48: "8", 0x49: "9",
          0x4A: "-", 0x4B: "4", 0x4C: "5", 0x4D: "6", 0x4E: "+", 0x4F: "1",
          0x50: "2", 0x51: "3", 0x52: "0", 0x53: "."}


def keymaps():
    """512 bytes: AZERTY normal, AZERTY shift, QWERTY normal, QWERTY shift."""
    out = bytearray(512)
    for base, table in ((0, AZERTY), (256, QWERTY)):
        for code, pair in table.items():
            out[base + code] = ord(pair[0])
            out[base + 128 + code] = ord(pair[1])
        for code, ch in COMMON.items():
            out[base + code] = ord(ch)
            out[base + 128 + code] = ord(ch)
    return bytes(out)


STRINGS = [
    ("BANNER", "Nexora OS 0.1 - noyau NTASM, programmes Nova"),
    ("OK", "[ OK ] "),
    ("FW", "Firmware UEFI quitte : la machine est a nous"),
    ("GDT", "GDT du noyau chargee (code 0x38, donnees 0x30)"),
    ("IDT", "IDT : 48 vecteurs generes par interrupt_table"),
    ("PIC", "PIC 8259 remappe sur les vecteurs 0x20-0x2F"),
    ("PIT", "Horloge PIT a 1000 Hz sur IRQ0"),
    ("KBD", "Clavier PS/2 sur IRQ1"),
    ("PAGING", "Pagination : identite 4 Gio, pile en pages de 4 Kio"),
    ("MEM", "Memoire physique libre : "),
    ("KIB", " Kio sur "),
    ("RAM", " Kio"),
    ("TSC", "Horloge TSC : "),
    ("KHZ", " kHz"),
    ("FB", "Ecran : "),
    ("X", "x"),
    ("PANIC", "PANIQUE NOYAU - exception "),
    ("RIP", "  rip="),
    ("ERR", "  erreur="),
    ("CR2", "  cr2="),
    ("TASK", "  tache="),
    ("HALT", "Machine arretee. Redemarre QEMU."),
    ("READY", "Tape au clavier : les touches arrivent par interruption."),
    ("UPTIME", " uptime "),
    ("TASKS", "  taches "),
    ("SWITCH", "  commutations "),
    ("SEC", "s"),
    ("GT", "> "),
    ("STATUS", " Nexora OS"),
    ("UP", "uptime "),
    ("S", "s"),
    ("IRQ", "  clavier "),
]


def string_offsets():
    offset = 0
    for key, value in STRINGS:
        yield key, offset
        offset += len(value) + 1


def literal(data):
    parts = []
    for b in data:
        if 32 <= b < 127 and b not in (34, 92):
            parts.append(chr(b))
        else:
            parts.append(f"\\x{b:02x}")
    return '"' + "".join(parts) + '"'


def main():
    out = Path(sys.argv[1])
    tables = {
        "palette": bytes(c for rgb in PALETTE for c in rgb),
        "font": font(),
        "keymaps": keymaps(),
        "strings": b"".join(v.encode("ascii") + b"\x00" for _, v in STRINGS),
    }
    lines = ["// Generated by tools/gen_assets.py - do not edit.",
             *[f"%define STR_{k} {o}" for k, o in string_offsets()],
             "section .rdata {"]
    for name, data in tables.items():
        lines.insert(1, f"%define {name.upper()}_LEN {len(data)}")
        lines.append(f"    data {name}:array<u8,{len(data)}> = {literal(data)}")
    lines.append("}")
    # Writable, zero-initialised kernel buffers (a .bss the assembler lacks).
    bss = {"g_spill": 16384}
    lines.append("section .data {")
    for name, size in bss.items():
        lines.append(f"    data {name}:array<u8,{size}> = {literal(bytes(size))}")
    lines.append("}")
    out.write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
