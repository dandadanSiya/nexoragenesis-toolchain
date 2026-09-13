#include "x64.h"
#include <limits.h>
#include <string.h>
#define TRY(expr)                                                              \
  do {                                                                         \
    NtX64Error e_ = (expr);                                                    \
    if (e_)                                                                    \
      return e_;                                                               \
  } while (0)
static int scalar_width(unsigned w) {
  return w == 8 || w == 16 || w == 32 || w == 64;
}
static void byte(NtX64Instruction *x, unsigned v) {
  x->bytes[x->length++] = (uint8_t)v;
}
static void little(NtX64Instruction *x, uint64_t v, unsigned n) {
  for (unsigned i = 0; i < n; i++)
    byte(x, (unsigned)(v >> (8 * i)));
}
static int fits(const NtX64Operand *o, unsigned w, int sign_extended) {
  if (w == 64)
    return 1;
  if (o->is_signed) {
    /* Compare unsigned two's-complement without implementation-defined casts.
     */
    uint64_t max = (UINT64_C(1) << (w - 1)) - 1, min = UINT64_MAX - max;
    return o->imm <= max || o->imm >= min;
  }
  return o->imm <= (sign_extended ? ((UINT64_C(1) << (w - 1)) - 1)
                                  : ((UINT64_C(1) << w) - 1));
}
static unsigned regcode(const NtX64Operand *o) {
  return o->reg + (o->high8 ? 4 : 0);
}
static unsigned regrex(const NtX64Operand *o) {
  return o->width == 8 && !o->high8 && o->reg >= 4;
}
static NtX64Error operand_valid(const NtX64Operand *o) {
  switch (o->kind) {
  case NT_X64_REG:
    if (!scalar_width(o->width))
      return NT_X64_WIDTH;
    if (o->reg > 15 || o->high8 > 1 ||
        (o->high8 && (o->width != 8 || o->reg > 3)))
      return NT_X64_REGISTER;
    return NT_X64_OK;
  case NT_X64_IMM:
  case NT_X64_REL:
    if (o->is_signed > 1)
      return NT_X64_ARGUMENT;
    if (o->width && !scalar_width(o->width))
      return NT_X64_WIDTH;
    if (o->width && !fits(o, o->width, 0))
      return NT_X64_RANGE;
    return NT_X64_OK;
  case NT_X64_MEM:
    if (!scalar_width(o->width) && o->width != 80 && o->width != 0)
      return NT_X64_WIDTH;
    if (o->base < -2 || o->base > 15 || o->index < -1 || o->index > 15 ||
        o->index == 4 ||
        (o->scale != 1 && o->scale != 2 && o->scale != 4 && o->scale != 8) ||
        (o->index == -1 && o->scale != 1) ||
        (o->base == -2 && o->index != -1) || o->disp < INT32_MIN ||
        o->disp > INT32_MAX)
      return NT_X64_ADDRESS;
    if (o->segment != NT_X64_SEG_NONE && o->segment != NT_X64_FS &&
        o->segment != NT_X64_GS)
      return NT_X64_SEGMENT;
    return NT_X64_OK;
  case NT_X64_CR:
    if (o->width != 64)
      return NT_X64_WIDTH;
    if (o->reg != 0 && o->reg != 2 && o->reg != 3 && o->reg != 4 && o->reg != 8)
      return NT_X64_REGISTER;
    if (o->high8)
      return NT_X64_REGISTER;
    return NT_X64_OK;
  default:
    return NT_X64_FORM;
  }
}
static NtX64Error prefix(NtX64Instruction *x, unsigned width, unsigned rex,
                         int high8, unsigned segment) {
  if (rex && high8)
    return NT_X64_HIGH8_REX;
  if (segment)
    byte(x, segment == NT_X64_FS ? 0x64 : 0x65);
  if (width == 16)
    byte(x, 0x66);
  if (rex)
    byte(x, 0x40 | rex);
  return NT_X64_OK;
}
/* rm field and opcode register field. reg=NULL means an opcode-extension digit.
 */
static NtX64Error modrm(NtX64Instruction *x, unsigned opcode, unsigned oplen,
                        unsigned width, const NtX64Operand *reg, unsigned digit,
                        const NtX64Operand *rm) {
  unsigned rex = width == 64 ? 8 : 0, rc = reg ? regcode(reg) : digit;
  if (rm->kind != NT_X64_REG && rm->kind != NT_X64_MEM)
    return NT_X64_FORM;
  if (reg && reg->reg >= 8)
    rex |= 4;
  if (rm->kind == NT_X64_REG) {
    if (rm->reg >= 8)
      rex |= 1;
    if (regrex(rm) || (reg && regrex(reg)))
      rex |= 0x40;
    TRY(prefix(x, width, rex, (int)(rm->high8 || (reg && reg->high8)), 0));
    little(x, opcode, oplen);
    byte(x, 0xc0 | ((rc & 7) << 3) | (regcode(rm) & 7));
  } else {
    unsigned mod = 0, dispbytes = 0, field, sib = 0, scale = 0;
    if (rm->base >= 8)
      rex |= 1;
    if (rm->index >= 8)
      rex |= 2;
    if (reg && regrex(reg))
      rex |= 0x40;
    TRY(prefix(x, width, rex, (int)(reg && reg->high8), rm->segment));
    little(x, opcode, oplen);
    if (rm->base == -2) {
      byte(x, ((rc & 7) << 3) | 5);
      x->displacement_offset = x->length;
      x->displacement_size = 4;
      little(x, (uint64_t)rm->disp, 4);
      return NT_X64_OK;
    }
    if (rm->base == -1)
      dispbytes = 4;
    else if (rm->disp == 0 && (rm->base & 7) != 5)
      mod = 0;
    else if (rm->disp >= -128 && rm->disp <= 127) {
      mod = 1;
      dispbytes = 1;
    } else {
      mod = 2;
      dispbytes = 4;
    }
    sib = rm->index != -1 || rm->base == -1 || (rm->base & 7) == 4;
    field = sib ? 4 : (unsigned)(rm->base & 7);
    byte(x, (mod << 6) | ((rc & 7) << 3) | field);
    if (sib) {
      for (unsigned v = rm->scale; v > 1; v >>= 1)
        scale++;
      byte(x, (scale << 6) |
                  ((rm->index == -1 ? 4 : (unsigned)(rm->index & 7)) << 3) |
                  (rm->base == -1 ? 5 : (unsigned)(rm->base & 7)));
    }
    if (dispbytes) {
      x->displacement_offset = x->length;
      x->displacement_size = dispbytes;
    }
    little(x, (uint64_t)rm->disp, dispbytes);
  }
  return NT_X64_OK;
}
static NtX64Error variable(const char *n, const NtX64Operand *o, size_t count,
                           NtX64Context ctx, NtX64Instruction *x) {
  if (!strcmp(n, "mov") || !strcmp(n, "load") || !strcmp(n, "store") ||
      !strcmp(n, "lea")) {
    if (count != 2)
      return NT_X64_COUNT;
    if (!(ctx.features & NT_X64_LM))
      return NT_X64_FEATURE;
    for (size_t i = 0; i < count; i++)
      TRY(operand_valid(&o[i]));
    if (!strcmp(n, "load") &&
        (o[0].kind != NT_X64_REG || o[1].kind != NT_X64_MEM))
      return NT_X64_FORM;
    if (!strcmp(n, "store") && o[0].kind != NT_X64_MEM)
      return NT_X64_FORM;
    if (!strcmp(n, "lea")) {
      if (o[0].kind != NT_X64_REG || o[1].kind != NT_X64_MEM)
        return NT_X64_FORM;
      if (o[0].width != 64)
        return NT_X64_WIDTH;
      return modrm(x, 0x8d, 1, 64, &o[0], 0, &o[1]);
    }
    if (o[0].kind == NT_X64_MEM) {
      unsigned w = o[0].width;
      if (!scalar_width(w))
        return NT_X64_WIDTH;
      if (o[1].kind == NT_X64_REG) {
        if (w != o[1].width)
          return NT_X64_WIDTH;
        return modrm(x, w == 8 ? 0x88 : 0x89, 1, w, &o[1], 0, &o[0]);
      }
      if (o[1].kind != NT_X64_IMM)
        return NT_X64_FORM;
      if (!fits(&o[1], w == 64 ? 32 : w, w == 64))
        return NT_X64_RANGE;
      TRY(modrm(x, w == 8 ? 0xc6 : 0xc7, 1, w, NULL, 0, &o[0]));
      little(x, o[1].imm, w == 64 ? 4 : w / 8);
      return NT_X64_OK;
    }
    if (o[0].kind != NT_X64_REG)
      return NT_X64_FORM;
    if (o[1].kind == NT_X64_MEM) {
      if (o[0].width != o[1].width)
        return NT_X64_WIDTH;
      return modrm(x, o[0].width == 8 ? 0x8a : 0x8b, 1, o[0].width, &o[0], 0,
                   &o[1]);
    }
    if (o[1].kind == NT_X64_REG) {
      if (o[0].width != o[1].width)
        return NT_X64_WIDTH;
      return modrm(x, o[0].width == 8 ? 0x88 : 0x89, 1, o[0].width, &o[1], 0,
                   &o[0]);
    }
    if (o[1].kind == NT_X64_IMM) {
      unsigned w = o[0].width, rex = (w == 64 ? 8 : 0) |
                                     (o[0].reg >= 8 ? 1 : 0) |
                                     (regrex(&o[0]) ? 0x40 : 0);
      if (!fits(&o[1], w, 0))
        return NT_X64_RANGE;
      TRY(prefix(x, w, rex, (int)o[0].high8, 0));
      byte(x, (w == 8 ? 0xb0 : 0xb8) + (regcode(&o[0]) & 7));
      little(x, o[1].imm, w / 8);
      return NT_X64_OK;
    }
    return NT_X64_FORM;
  }
  {
    static const char *names[] = {"add", "or",  "adc", "sbb",
                                  "and", "sub", "xor", "cmp"};
    unsigned group = 8;
    for (unsigned i = 0; i < 8; i++)
      if (!strcmp(n, names[i]))
        group = i;
    if (group < 8) {
      if (count != 2)
        return NT_X64_COUNT;
      if (!(ctx.features & NT_X64_LM))
        return NT_X64_FEATURE;
      TRY(operand_valid(&o[0]));
      TRY(operand_valid(&o[1]));
      if (o[0].kind != NT_X64_REG && o[0].kind != NT_X64_MEM)
        return NT_X64_FORM;
      unsigned w = o[0].width;
      if (!scalar_width(w))
        return NT_X64_WIDTH;
      if (o[1].kind == NT_X64_REG) {
        if (o[1].width != w)
          return NT_X64_WIDTH;
        return modrm(x, group * 8 + (w == 8 ? 0 : 1), 1, w, &o[1], 0, &o[0]);
      }
      if (o[1].kind == NT_X64_MEM) {
        if (o[0].kind != NT_X64_REG)
          return NT_X64_FORM;
        if (o[1].width != w)
          return NT_X64_WIDTH;
        return modrm(x, group * 8 + (w == 8 ? 2 : 3), 1, w, &o[0], 0, &o[1]);
      }
      if (o[1].kind != NT_X64_IMM)
        return NT_X64_FORM;
      if (w == 64 && !fits(&o[1], 32, 1))
        return NT_X64_RANGE;
      if (w < 64 && !fits(&o[1], w, 0))
        return NT_X64_RANGE;
      if (o[0].kind == NT_X64_REG && o[0].reg == 0 && !o[0].high8 &&
          (w == 8 || !fits(&o[1], 8, 1))) {
        unsigned rex = w == 64 ? 8 : 0;
        TRY(prefix(x, w, rex, 0, 0));
        byte(x, group * 8 + (w == 8 ? 4 : 5));
        little(x, o[1].imm, w == 8 ? 1 : (w == 16 ? 2 : 4));
        return NT_X64_OK;
      }
      unsigned compact = fits(&o[1], 8, 1);
      TRY(modrm(x, w == 8 ? 0x80 : (compact ? 0x83 : 0x81), 1, w, NULL, group,
                &o[0]));
      little(x, o[1].imm, compact || w == 8 ? 1 : (w == 16 ? 2 : 4));
      return NT_X64_OK;
    }
  }
  if (!strcmp(n, "test")) {
    if (count != 2)
      return NT_X64_COUNT;
    if (!(ctx.features & NT_X64_LM))
      return NT_X64_FEATURE;
    TRY(operand_valid(&o[0]));
    TRY(operand_valid(&o[1]));
    if (o[0].kind != NT_X64_REG && o[0].kind != NT_X64_MEM)
      return NT_X64_FORM;
    unsigned w = o[0].width;
    if (o[1].kind == NT_X64_REG) {
      if (o[1].width != w)
        return NT_X64_WIDTH;
      return modrm(x, w == 8 ? 0x84 : 0x85, 1, w, &o[1], 0, &o[0]);
    }
    if (o[1].kind != NT_X64_IMM)
      return NT_X64_FORM;
    if (w == 64 && !fits(&o[1], 32, 1))
      return NT_X64_RANGE;
    if (w < 64 && !fits(&o[1], w, 0))
      return NT_X64_RANGE;
    if (o[0].kind == NT_X64_REG && o[0].reg == 0 && !o[0].high8) {
      TRY(prefix(x, w, w == 64 ? 8 : 0, 0, 0));
      byte(x, w == 8 ? 0xa8 : 0xa9);
    } else
      TRY(modrm(x, w == 8 ? 0xf6 : 0xf7, 1, w, NULL, 0, &o[0]));
    little(x, o[1].imm, w == 8 ? 1 : (w == 16 ? 2 : 4));
    return NT_X64_OK;
  }
  if (!strcmp(n, "inc") || !strcmp(n, "dec") || !strcmp(n, "not") ||
      !strcmp(n, "neg")) {
    if (count != 1)
      return NT_X64_COUNT;
    if (!(ctx.features & NT_X64_LM))
      return NT_X64_FEATURE;
    TRY(operand_valid(&o[0]));
    if (o[0].kind != NT_X64_REG && o[0].kind != NT_X64_MEM)
      return NT_X64_FORM;
    unsigned digit = !strcmp(n, "inc")   ? 0
                     : !strcmp(n, "dec") ? 1
                     : !strcmp(n, "not") ? 2
                                         : 3;
    unsigned opcode = digit < 2 ? (o[0].width == 8 ? 0xfe : 0xff)
                                : (o[0].width == 8 ? 0xf6 : 0xf7);
    return modrm(x, opcode, 1, o[0].width, NULL, digit, &o[0]);
  }
  if (!strcmp(n, "shl") || !strcmp(n, "shr") || !strcmp(n, "sar") ||
      !strcmp(n, "rol") || !strcmp(n, "ror")) {
    if (count != 2)
      return NT_X64_COUNT;
    if (!(ctx.features & NT_X64_LM))
      return NT_X64_FEATURE;
    TRY(operand_valid(&o[0]));
    TRY(operand_valid(&o[1]));
    if (o[0].kind != NT_X64_REG && o[0].kind != NT_X64_MEM)
      return NT_X64_FORM;
    unsigned digit = !strcmp(n, "rol")   ? 0
                     : !strcmp(n, "ror") ? 1
                     : !strcmp(n, "shl") ? 4
                     : !strcmp(n, "shr") ? 5
                                         : 7;
    unsigned w = o[0].width;
    if (o[1].kind == NT_X64_REG) {
      if (o[1].reg != 1 || o[1].width != 8 || o[1].high8)
        return NT_X64_REGISTER;
      return modrm(x, w == 8 ? 0xd2 : 0xd3, 1, w, NULL, digit, &o[0]);
    }
    if (o[1].kind != NT_X64_IMM)
      return NT_X64_FORM;
    if (o[1].imm > 255)
      return NT_X64_RANGE;
    if (o[1].imm == 1)
      return modrm(x, w == 8 ? 0xd0 : 0xd1, 1, w, NULL, digit, &o[0]);
    TRY(modrm(x, w == 8 ? 0xc0 : 0xc1, 1, w, NULL, digit, &o[0]));
    byte(x, (unsigned)o[1].imm);
    return NT_X64_OK;
  }
  if (!strcmp(n, "imul")) {
    if (count != 2 && count != 3)
      return NT_X64_COUNT;
    if (!(ctx.features & NT_X64_LM))
      return NT_X64_FEATURE;
    for (size_t i = 0; i < count; i++)
      TRY(operand_valid(&o[i]));
    if (o[0].kind != NT_X64_REG ||
        (o[1].kind != NT_X64_REG && o[1].kind != NT_X64_MEM))
      return NT_X64_FORM;
    if (o[0].width == 8 || o[0].width != o[1].width)
      return NT_X64_WIDTH;
    if (count == 2)
      return modrm(x, 0xaf0f, 2, o[0].width, &o[0], 0, &o[1]);
    if (o[2].kind != NT_X64_IMM)
      return NT_X64_FORM;
    if (!fits(&o[2], 32, 1))
      return NT_X64_RANGE;
    unsigned compact = fits(&o[2], 8, 1);
    TRY(modrm(x, compact ? 0x6b : 0x69, 1, o[0].width, &o[0], 0, &o[1]));
    little(x, o[2].imm, compact ? 1 : 4);
    return NT_X64_OK;
  }
  if (!strcmp(n, "mul") || !strcmp(n, "div") || !strcmp(n, "idiv")) {
    if (count != 1)
      return NT_X64_COUNT;
    if (!(ctx.features & NT_X64_LM))
      return NT_X64_FEATURE;
    TRY(operand_valid(&o[0]));
    if (o[0].kind != NT_X64_REG && o[0].kind != NT_X64_MEM)
      return NT_X64_FORM;
    unsigned digit = !strcmp(n, "mul") ? 4 : !strcmp(n, "div") ? 6 : 7;
    return modrm(x, o[0].width == 8 ? 0xf6 : 0xf7, 1, o[0].width, NULL, digit,
                 &o[0]);
  }
  if (!strcmp(n, "push") || !strcmp(n, "pop")) {
    if (count != 1)
      return NT_X64_COUNT;
    if (!(ctx.features & NT_X64_LM))
      return NT_X64_FEATURE;
    TRY(operand_valid(&o[0]));
    int pushing = !strcmp(n, "push");
    if (o[0].kind == NT_X64_REG) {
      if (o[0].width != 64 || o[0].high8)
        return NT_X64_WIDTH;
      if (o[0].reg >= 8)
        byte(x, 0x41);
      byte(x, (pushing ? 0x50 : 0x58) + (o[0].reg & 7));
      return NT_X64_OK;
    }
    if (o[0].kind == NT_X64_MEM) {
      if (o[0].width != 64)
        return NT_X64_WIDTH;
      return modrm(x, pushing ? 0xff : 0x8f, 1, 32, NULL, pushing ? 6 : 0,
                   &o[0]);
    }
    if (pushing && o[0].kind == NT_X64_IMM) {
      if (fits(&o[0], 8, 1)) {
        byte(x, 0x6a);
        byte(x, (unsigned)o[0].imm);
        return NT_X64_OK;
      }
      if (!fits(&o[0], 32, 1))
        return NT_X64_RANGE;
      byte(x, 0x68);
      little(x, o[0].imm, 4);
      return NT_X64_OK;
    }
    return NT_X64_FORM;
  }
  if (!strcmp(n, "call") || !strcmp(n, "jmp")) {
    if (count != 1)
      return NT_X64_COUNT;
    if (!(ctx.features & NT_X64_LM))
      return NT_X64_FEATURE;
    TRY(operand_valid(&o[0]));
    if (o[0].kind == NT_X64_REL) {
      if (!fits(&o[0], 32, 1))
        return NT_X64_RANGE;
      byte(x, !strcmp(n, "call") ? 0xe8 : 0xe9);
      little(x, o[0].imm, 4);
      return NT_X64_OK;
    }
    if (o[0].kind != NT_X64_REG && o[0].kind != NT_X64_MEM)
      return NT_X64_FORM;
    if (o[0].width != 64)
      return NT_X64_WIDTH;
    return modrm(x, 0xff, 1, 32, NULL, !strcmp(n, "call") ? 2 : 4, &o[0]);
  }
  {
    static const char *names[] = {"jo",  "jno", "jb",  "jae", "je", "jne",
                                  "jbe", "ja",  "js",  "jns", "jp", "jnp",
                                  "jl",  "jge", "jle", "jg"};
    unsigned condition = 16;
    for (unsigned i = 0; i < 16; i++)
      if (!strcmp(n, names[i]))
        condition = i;
    if (condition < 16) {
      if (count != 1)
        return NT_X64_COUNT;
      if (!(ctx.features & NT_X64_LM))
        return NT_X64_FEATURE;
      TRY(operand_valid(&o[0]));
      if (o[0].kind != NT_X64_REL)
        return NT_X64_FORM;
      if (!fits(&o[0], 32, 1))
        return NT_X64_RANGE;
      byte(x, 0x0f);
      byte(x, 0x80 + condition);
      little(x, o[0].imm, 4);
      return NT_X64_OK;
    }
  }
  if (!strcmp(n, "read_cr") || !strcmp(n, "write_cr")) {
    if (count != 2)
      return NT_X64_COUNT;
    if (!(ctx.features & NT_X64_LM))
      return NT_X64_FEATURE;
    if (ctx.cpl)
      return NT_X64_PRIVILEGE;
    TRY(operand_valid(&o[0]));
    TRY(operand_valid(&o[1]));
    const NtX64Operand *gpr = !strcmp(n, "read_cr") ? &o[0] : &o[1];
    const NtX64Operand *control = !strcmp(n, "read_cr") ? &o[1] : &o[0];
    if (gpr->kind != NT_X64_REG || control->kind != NT_X64_CR)
      return NT_X64_FORM;
    unsigned rex = (gpr->reg >= 8 ? 1 : 0) | (control->reg == 8 ? 4 : 0);
    if (rex)
      byte(x, 0x40 | rex);
    byte(x, 0x0f);
    byte(x, !strcmp(n, "read_cr") ? 0x20 : 0x22);
    byte(x, 0xc0 | ((control->reg & 7) << 3) | (gpr->reg & 7));
    return NT_X64_OK;
  }
  if (!strcmp(n, "movzx") || !strcmp(n, "movsx") || !strcmp(n, "movsxd")) {
    if (count != 2)
      return NT_X64_COUNT;
    if (!(ctx.features & NT_X64_LM))
      return NT_X64_FEATURE;
    TRY(operand_valid(&o[0]));
    TRY(operand_valid(&o[1]));
    if (o[0].kind != NT_X64_REG ||
        (o[1].kind != NT_X64_REG && o[1].kind != NT_X64_MEM))
      return NT_X64_FORM;
    unsigned source_width = o[1].width, dest_width = o[0].width;
    if (!strcmp(n, "movsxd")) {
      if (source_width != 32 || dest_width != 64)
        return NT_X64_WIDTH;
      return modrm(x, 0x63, 1, 64, &o[0], 0, &o[1]);
    }
    if ((source_width != 8 && source_width != 16) ||
        dest_width <= source_width ||
        (dest_width != 16 && dest_width != 32 && dest_width != 64))
      return NT_X64_WIDTH;
    unsigned opcode = !strcmp(n, "movzx")
                          ? (source_width == 8 ? 0xb60f : 0xb70f)
                          : (source_width == 8 ? 0xbe0f : 0xbf0f);
    return modrm(x, opcode, 2, dest_width, &o[0], 0, &o[1]);
  }
  {
    static const char *names[] = {
        "sete", "setne", "setb", "setae", "setbe", "seta", "sets", "setns",
        "setp", "setnp", "setl", "setge", "setle", "setg", "seto", "setno"};
    static const unsigned conditions[] = {4,  5,  2,  3,  6,  7,  8, 9,
                                          10, 11, 12, 13, 14, 15, 0, 1};
    unsigned cc = 16;
    for (unsigned i = 0; i < 16; i++)
      if (!strcmp(n, names[i]))
        cc = conditions[i];
    if (cc < 16) {
      if (count != 1)
        return NT_X64_COUNT;
      if (!(ctx.features & NT_X64_LM))
        return NT_X64_FEATURE;
      TRY(operand_valid(&o[0]));
      if ((o[0].kind != NT_X64_REG && o[0].kind != NT_X64_MEM))
        return NT_X64_FORM;
      if (o[0].width != 8)
        return NT_X64_WIDTH;
      return modrm(x, 0x900f + ((unsigned)cc << 8), 2, 8, NULL, 0, &o[0]);
    }
  }
  {
    static const char *names[] = {"cmove",  "cmovne", "cmovb", "cmovae",
                                  "cmovbe", "cmova",  "cmovs", "cmovns",
                                  "cmovp",  "cmovnp", "cmovl", "cmovge",
                                  "cmovle", "cmovg",  "cmovo", "cmovno"};
    static const unsigned conditions[] = {4,  5,  2,  3,  6,  7,  8, 9,
                                          10, 11, 12, 13, 14, 15, 0, 1};
    unsigned cc = 16;
    for (unsigned i = 0; i < 16; i++)
      if (!strcmp(n, names[i]))
        cc = conditions[i];
    if (cc < 16) {
      if (count != 2)
        return NT_X64_COUNT;
      if (!(ctx.features & NT_X64_LM))
        return NT_X64_FEATURE;
      TRY(operand_valid(&o[0]));
      TRY(operand_valid(&o[1]));
      if (o[0].kind != NT_X64_REG ||
          (o[1].kind != NT_X64_REG && o[1].kind != NT_X64_MEM))
        return NT_X64_FORM;
      if (o[0].width == 8 || o[0].width != o[1].width)
        return NT_X64_WIDTH;
      return modrm(x, 0x400f + ((unsigned)cc << 8), 2, o[0].width, &o[0], 0,
                   &o[1]);
    }
  }
  if (!strcmp(n, "bswap")) {
    if (count != 1)
      return NT_X64_COUNT;
    if (!(ctx.features & NT_X64_LM))
      return NT_X64_FEATURE;
    TRY(operand_valid(&o[0]));
    if (o[0].kind != NT_X64_REG)
      return NT_X64_FORM;
    if (o[0].width != 32 && o[0].width != 64)
      return NT_X64_WIDTH;
    unsigned rex = (o[0].width == 64 ? 8 : 0) | (o[0].reg >= 8 ? 1 : 0);
    TRY(prefix(x, o[0].width, rex, 0, 0));
    byte(x, 0x0f);
    byte(x, 0xc8 + (o[0].reg & 7));
    return NT_X64_OK;
  }
  {
    static const char *names[] = {"bt", "bts", "btr", "btc"};
    static const unsigned reg_opcodes[] = {0xa3, 0xab, 0xb3, 0xbb};
    unsigned operation = 4;
    for (unsigned i = 0; i < 4; i++)
      if (!strcmp(n, names[i]))
        operation = i;
    if (operation < 4) {
      if (count != 2)
        return NT_X64_COUNT;
      if (!(ctx.features & NT_X64_LM))
        return NT_X64_FEATURE;
      TRY(operand_valid(&o[0]));
      TRY(operand_valid(&o[1]));
      if ((o[0].kind != NT_X64_REG && o[0].kind != NT_X64_MEM) ||
          o[0].width == 8)
        return NT_X64_FORM;
      if (o[1].kind == NT_X64_REG) {
        if (o[1].width != o[0].width)
          return NT_X64_WIDTH;
        return modrm(x, 0x0f + (reg_opcodes[operation] << 8), 2, o[0].width,
                     &o[1], 0, &o[0]);
      }
      if (o[1].kind != NT_X64_IMM)
        return NT_X64_FORM;
      if (o[1].imm > 255)
        return NT_X64_RANGE;
      TRY(modrm(x, 0xba0f, 2, o[0].width, NULL, 4 + operation, &o[0]));
      byte(x, (unsigned)o[1].imm);
      return NT_X64_OK;
    }
  }
  if (!strcmp(n, "bsf") || !strcmp(n, "bsr")) {
    if (count != 2)
      return NT_X64_COUNT;
    if (!(ctx.features & NT_X64_LM))
      return NT_X64_FEATURE;
    TRY(operand_valid(&o[0]));
    TRY(operand_valid(&o[1]));
    if (o[0].kind != NT_X64_REG ||
        (o[1].kind != NT_X64_REG && o[1].kind != NT_X64_MEM))
      return NT_X64_FORM;
    if (o[0].width == 8 || o[0].width != o[1].width)
      return NT_X64_WIDTH;
    return modrm(x, !strcmp(n, "bsf") ? 0xbc0f : 0xbd0f, 2, o[0].width, &o[0],
                 0, &o[1]);
  }
  if (!strcmp(n, "xchg")) {
    if (count != 2)
      return NT_X64_COUNT;
    if (!(ctx.features & NT_X64_LM))
      return NT_X64_FEATURE;
    TRY(operand_valid(&o[0]));
    TRY(operand_valid(&o[1]));
    if (o[0].kind != NT_X64_REG ||
        (o[1].kind != NT_X64_REG && o[1].kind != NT_X64_MEM))
      return NT_X64_FORM;
    if (o[0].width != o[1].width)
      return NT_X64_WIDTH;
    if (o[1].kind == NT_X64_REG)
      return modrm(x, o[0].width == 8 ? 0x86 : 0x87, 1, o[0].width, &o[1], 0,
                   &o[0]);
    return modrm(x, o[0].width == 8 ? 0x86 : 0x87, 1, o[0].width, &o[0], 0,
                 &o[1]);
  }
  {
    static const char *names[] = {"sgdt", "sidt", "lgdt", "lidt"};
    unsigned digit = 4;
    for (unsigned i = 0; i < 4; i++)
      if (!strcmp(n, names[i]))
        digit = i;
    if (digit < 4) {
      if (count != 1)
        return NT_X64_COUNT;
      if (!(ctx.features & NT_X64_LM))
        return NT_X64_FEATURE;
      if (ctx.cpl && digit >= 2)
        return NT_X64_PRIVILEGE;
      TRY(operand_valid(&o[0]));
      if (o[0].kind != NT_X64_MEM)
        return NT_X64_FORM;
      if (o[0].width != 80)
        return NT_X64_WIDTH;
      return modrm(x, 0x010f, 2, 32, NULL, digit, &o[0]);
    }
  }
  if (!strcmp(n, "ltr")) {
    if (count != 1)
      return NT_X64_COUNT;
    if (!(ctx.features & NT_X64_LM))
      return NT_X64_FEATURE;
    if (ctx.cpl)
      return NT_X64_PRIVILEGE;
    TRY(operand_valid(&o[0]));
    if ((o[0].kind != NT_X64_REG && o[0].kind != NT_X64_MEM))
      return NT_X64_FORM;
    if (o[0].width != 16)
      return NT_X64_WIDTH;
    return modrm(x, 0x000f, 2, 32, NULL, 3, &o[0]);
  }
  if (!strcmp(n, "invlpg")) {
    if (count != 1)
      return NT_X64_COUNT;
    if (!(ctx.features & NT_X64_LM))
      return NT_X64_FEATURE;
    if (ctx.cpl)
      return NT_X64_PRIVILEGE;
    TRY(operand_valid(&o[0]));
    if (o[0].kind != NT_X64_MEM)
      return NT_X64_FORM;
    if (o[0].width != 0)
      return NT_X64_WIDTH;
    return modrm(x, 0x010f, 2, 32, NULL, 7, &o[0]);
  }
  if (!strcmp(n, "in") || !strcmp(n, "out")) {
    if (count != 2)
      return NT_X64_COUNT;
    if (!(ctx.features & NT_X64_LM))
      return NT_X64_FEATURE;
    if (ctx.cpl)
      return NT_X64_PRIVILEGE;
    for (size_t i = 0; i < count; i++)
      TRY(operand_valid(&o[i]));
    const NtX64Operand *acc = !strcmp(n, "in") ? &o[0] : &o[1];
    const NtX64Operand *port = !strcmp(n, "in") ? &o[1] : &o[0];
    if (acc->kind != NT_X64_REG || acc->reg != 0 || acc->high8 ||
        (acc->width != 8 && acc->width != 16 && acc->width != 32))
      return NT_X64_REGISTER;
    if (acc->width == 16)
      byte(x, 0x66);
    if (port->kind == NT_X64_IMM) {
      if (port->imm > 255)
        return NT_X64_RANGE;
      byte(x, !strcmp(n, "in") ? (acc->width == 8 ? 0xe4 : 0xe5)
                               : (acc->width == 8 ? 0xe6 : 0xe7));
      byte(x, (unsigned)port->imm);
      return NT_X64_OK;
    }
    if (port->kind != NT_X64_REG || port->reg != 2 || port->width != 16 ||
        port->high8)
      return NT_X64_REGISTER;
    byte(x, !strcmp(n, "in") ? (acc->width == 8 ? 0xec : 0xed)
                             : (acc->width == 8 ? 0xee : 0xef));
    return NT_X64_OK;
  }
  return NT_X64_UNKNOWN;
}
typedef struct {
  const char *name;
  unsigned bytes, length, features, cpl;
} Fixed;
static const Fixed fixed[] = {
    {"nop", 0x90, 1, 1, 3},        {"pause", 0x90f3, 2, 1, 3},
    {"ud2", 0x0b0f, 2, 1, 3},      {"cpuid", 0xa20f, 2, 1, 3},
    {"cbw", 0x9866, 2, 1, 3},      {"cwde", 0x98, 1, 1, 3},
    {"cdqe", 0x9848, 2, 1, 3},     {"cwd", 0x9966, 2, 1, 3},
    {"cdq", 0x99, 1, 1, 3},        {"cqo", 0x9948, 2, 1, 3},
    {"pushfq", 0x9c, 1, 1, 3},     {"popfq", 0x9d, 1, 1, 0},
    {"rdtsc", 0x310f, 2, 17, 0},   {"lfence", 0xe8ae0f, 3, 9, 3},
    {"sfence", 0xf8ae0f, 3, 9, 3}, {"mfence", 0xf0ae0f, 3, 9, 3},
    {"cli", 0xfa, 1, 1, 0},        {"sti", 0xfb, 1, 1, 0},
    {"hlt", 0xf4, 1, 1, 0},        {"swapgs", 0xf8010f, 3, 1, 0},
    {"rdmsr", 0x320f, 2, 3, 0},    {"wrmsr", 0x300f, 2, 3, 0},
    {"syscall", 0x050f, 2, 5, 3},  {"sysretq", 0x070f48, 3, 5, 0},
    {"iretq", 0xcf48, 2, 1, 0},    {"ret", 0xc3, 1, 1, 3},
    {"return", 0xc3, 1, 1, 3}};
NtX64Error nt_x64_encode(const char *mnemonic, const NtX64Operand *operands,
                         size_t count, NtX64Context context,
                         NtX64Instruction *out) {
  if (!mnemonic || !out || (count && !operands))
    return NT_X64_ARGUMENT;
  if (count > 3)
    return NT_X64_COUNT;
  if (context.cpl > 3)
    return NT_X64_PRIVILEGE;
  for (size_t i = 0; i < sizeof fixed / sizeof *fixed; i++)
    if (!strcmp(mnemonic, fixed[i].name)) {
      NtX64Instruction x = {0};
      const Fixed *f = &fixed[i];
      if (count)
        return NT_X64_COUNT;
      if ((context.features & f->features) != f->features)
        return NT_X64_FEATURE;
      if (context.cpl > f->cpl)
        return NT_X64_PRIVILEGE;
      for (unsigned k = 0; k < f->length; k++)
        x.bytes[x.length++] = (uint8_t)(f->bytes >> (8 * k));
      *out = x;
      return NT_X64_OK;
    }
  NtX64Instruction x = {0};
  NtX64Error error = variable(mnemonic, operands, count, context, &x);
  if (!error)
    *out = x;
  return error;
}
const char *nt_x64_error_string(NtX64Error error) {
  static const char *messages[] = {"ok",
                                   "unknown mnemonic",
                                   "wrong operand count",
                                   "operand width mismatch",
                                   "missing CPU feature",
                                   "privilege violation",
                                   "invalid register",
                                   "value out of range",
                                   "high 8-bit register requires no REX",
                                   "invalid operand form",
                                   "invalid segment",
                                   "invalid address",
                                   "invalid argument"};
  unsigned index = (unsigned)error;
  return index >= NT_X64_UNKNOWN && index <= NT_X64_ARGUMENT
             ? messages[index - NT_X64_UNKNOWN + 1]
             : messages[0];
}

static int member(const char *name, const char *list) {
  size_t n = strlen(name);
  for (const char *start = list; *start;) {
    const char *end = strchr(start, '|');
    size_t length = end ? (size_t)(end - start) : strlen(start);
    if (length == n && !memcmp(start, name, n))
      return 1;
    if (!end)
      break;
    start = end + 1;
  }
  return 0;
}
static void read_operand(NtX64Effects *e, const NtX64Operand *o) {
  if (o->kind == NT_X64_REG)
    e->registers_read |= UINT64_C(1) << o->reg;
  else if (o->kind == NT_X64_MEM)
    e->memory_read = 1;
}
static void write_operand(NtX64Effects *e, const NtX64Operand *o) {
  if (o->kind == NT_X64_REG) {
    e->registers_written |= UINT64_C(1) << o->reg;
    if (o->width < 32)
      e->registers_read |= UINT64_C(1) << o->reg;
  } else if (o->kind == NT_X64_MEM)
    e->memory_write = 1;
}
static unsigned condition_flags(const char *suffix) {
  if (member(suffix, "o|no"))
    return NT_X64_FLAG_OF;
  if (member(suffix, "b|ae"))
    return NT_X64_FLAG_CF;
  if (member(suffix, "e|ne"))
    return NT_X64_FLAG_ZF;
  if (member(suffix, "be|a"))
    return NT_X64_FLAG_CF | NT_X64_FLAG_ZF;
  if (member(suffix, "s|ns"))
    return NT_X64_FLAG_SF;
  if (member(suffix, "p|np"))
    return NT_X64_FLAG_PF;
  if (member(suffix, "l|ge"))
    return NT_X64_FLAG_SF | NT_X64_FLAG_OF;
  return NT_X64_FLAG_ZF | NT_X64_FLAG_SF | NT_X64_FLAG_OF;
}
NtX64Error nt_x64_effects(const char *n, const NtX64Operand *o, size_t count,
                          NtX64Context context, NtX64Effects *out) {
  if (!out)
    return NT_X64_ARGUMENT;
  NtX64Instruction encoded;
  NtX64Error error = nt_x64_encode(n, o, count, context, &encoded);
  if (error)
    return error;
  NtX64Effects e = {0};
  e.required_features = NT_X64_LM;
  e.max_cpl = 3;
  for (size_t i = 0; i < sizeof fixed / sizeof fixed[0]; i++)
    if (!strcmp(n, fixed[i].name)) {
      e.required_features = fixed[i].features;
      e.max_cpl = fixed[i].cpl;
    }
  for (size_t i = 0; i < count; i++)
    if (o[i].kind == NT_X64_MEM) {
      if (o[i].base >= 0)
        e.registers_read |= UINT64_C(1) << o[i].base;
      if (o[i].index >= 0)
        e.registers_read |= UINT64_C(1) << o[i].index;
      if (o[i].segment)
        e.registers_read |= UINT64_C(1)
                            << (o[i].segment == NT_X64_FS ? 19 : 18);
    }
  if (member(n, "mov|load|store|movzx|movsx|movsxd")) {
    read_operand(&e, &o[1]);
    write_operand(&e, &o[0]);
  } else if (!strcmp(n, "lea"))
    write_operand(&e, &o[0]);
  else if (member(n, "add|adc|sub|sbb|and|or|xor|cmp|test")) {
    read_operand(&e, &o[0]);
    read_operand(&e, &o[1]);
    if (!member(n, "cmp|test"))
      write_operand(&e, &o[0]);
    e.flags_written = NT_X64_FLAGS_ARITH;
    if (member(n, "adc|sbb"))
      e.flags_read = NT_X64_FLAG_CF;
    if (member(n, "and|or|xor|test")) {
      e.flags_written &= ~NT_X64_FLAG_AF;
      e.flags_undefined = NT_X64_FLAG_AF;
    }
  } else if (member(n, "inc|dec|neg|not")) {
    read_operand(&e, &o[0]);
    write_operand(&e, &o[0]);
    if (strcmp(n, "not"))
      e.flags_written = NT_X64_FLAGS_ARITH;
    if (member(n, "inc|dec"))
      e.flags_written &= ~NT_X64_FLAG_CF;
  } else if (member(n, "shl|shr|sar|rol|ror")) {
    read_operand(&e, &o[0]);
    read_operand(&e, &o[1]);
    write_operand(&e, &o[0]);
    unsigned amount = o[1].kind == NT_X64_IMM ? (unsigned)o[1].imm : UINT_MAX;
    if (amount != UINT_MAX) {
      amount &= o[0].width == 64 ? 63u : 31u;
      if (member(n, "rol|ror"))
        amount %= o[0].width;
    }
    if (amount) {
      e.flags_written = NT_X64_FLAG_CF;
      if (!member(n, "rol|ror")) {
        e.flags_written |= NT_X64_FLAG_PF | NT_X64_FLAG_ZF | NT_X64_FLAG_SF;
        e.flags_undefined = NT_X64_FLAG_AF;
        if (amount == UINT_MAX || amount > o[0].width) {
          e.flags_written &= ~NT_X64_FLAG_CF;
          e.flags_undefined |= NT_X64_FLAG_CF;
        }
      }
      if (amount == 1)
        e.flags_written |= NT_X64_FLAG_OF;
      else
        e.flags_undefined |= NT_X64_FLAG_OF;
    }
    /* CL may be zero, in which case no flag is defined by this instruction.
     * A static footprint cannot establish any new fact without knowing CL. */
    if (amount == UINT_MAX) {
      e.flags_written = 0;
      e.flags_undefined = member(n, "rol|ror") ? NT_X64_FLAG_CF | NT_X64_FLAG_OF
                                               : NT_X64_FLAGS_ARITH;
    }
  } else if (!strcmp(n, "imul")) {
    if (count == 2)
      read_operand(&e, &o[0]);
    read_operand(&e, &o[1]);
    write_operand(&e, &o[0]);
    e.flags_written = NT_X64_FLAG_CF | NT_X64_FLAG_OF;
    e.flags_undefined = NT_X64_FLAGS_ARITH & ~e.flags_written;
  } else if (member(n, "mul|div|idiv")) {
    read_operand(&e, &o[0]);
    e.registers_read |= 1;
    e.registers_written |= o[0].width == 8 ? 1u : 5u;
    if (strcmp(n, "mul")) {
      e.registers_read |= o[0].width == 8 ? 1u : 5u;
      e.flags_undefined = NT_X64_FLAGS_ARITH;
      e.may_trap = 1;
    } else {
      e.flags_written = NT_X64_FLAG_CF | NT_X64_FLAG_OF;
      e.flags_undefined = NT_X64_FLAGS_ARITH & ~e.flags_written;
    }
  } else if (member(n, "cbw|cwde|cdqe|cwd|cdq|cqo")) {
    e.registers_read |= 1;
    e.registers_written |= member(n, "cwd|cdq|cqo") ? 4u : 1u;
  } else if (member(n, "push|pushfq|pop|popfq|ret|return|call")) {
    e.registers_read |= UINT64_C(1) << 4;
    e.registers_written |= UINT64_C(1) << 4;
    if (member(n, "push|pushfq|call"))
      e.stack_write = 1;
    else
      e.stack_read = 1;
    if (!strcmp(n, "push"))
      read_operand(&e, &o[0]);
    if (!strcmp(n, "pop"))
      write_operand(&e, &o[0]);
    if (!strcmp(n, "pushfq"))
      e.registers_read |= UINT64_C(1) << 17;
    if (!strcmp(n, "popfq"))
      e.flags_written = NT_X64_FLAGS_ARITH;
    if (!strcmp(n, "call"))
      read_operand(&e, &o[0]);
    if (member(n, "ret|return"))
      e.terminal = 1;
  } else if (n[0] == 'j') {
    read_operand(&e, &o[0]);
    e.terminal = !strcmp(n, "jmp");
    if (strcmp(n, "jmp")) {
      e.flags_read = condition_flags(n + 1);
      e.conditional = 1;
    }
  } else if (!strncmp(n, "set", 3)) {
    e.flags_read = condition_flags(n + 3);
    write_operand(&e, &o[0]);
  } else if (!strncmp(n, "cmov", 4)) {
    e.flags_read = condition_flags(n + 4);
    read_operand(&e, &o[0]);
    read_operand(&e, &o[1]);
    write_operand(&e, &o[0]);
    e.conditional = 1;
  } else if (!strcmp(n, "bswap")) {
    read_operand(&e, &o[0]);
    write_operand(&e, &o[0]);
  } else if (member(n, "bt|bts|btr|btc")) {
    read_operand(&e, &o[0]);
    read_operand(&e, &o[1]);
    if (strcmp(n, "bt"))
      write_operand(&e, &o[0]);
    e.flags_written = NT_X64_FLAG_CF;
    e.flags_undefined =
        NT_X64_FLAG_OF | NT_X64_FLAG_SF | NT_X64_FLAG_AF | NT_X64_FLAG_PF;
  } else if (member(n, "bsf|bsr")) {
    read_operand(&e, &o[1]);
    write_operand(&e, &o[0]);
    e.flags_written = NT_X64_FLAG_ZF;
    e.flags_undefined = NT_X64_FLAGS_ARITH & ~NT_X64_FLAG_ZF;
    e.destination_requires_nonzero = 1;
  } else if (!strcmp(n, "xchg")) {
    read_operand(&e, &o[0]);
    read_operand(&e, &o[1]);
    write_operand(&e, &o[0]);
    write_operand(&e, &o[1]);
    if (e.memory_write)
      e.effects |= NT_X64_EFFECT_MEMORY_ORDERING;
  } else if (!strcmp(n, "cpuid")) {
    e.registers_read = 3;
    e.registers_written = 15;
  } else if (!strcmp(n, "rdtsc")) {
    e.registers_written = 5;
    e.effects = NT_X64_EFFECT_READS_CLOCK;
  } else if (member(n, "lfence|sfence|mfence"))
    e.effects = NT_X64_EFFECT_MEMORY_ORDERING;
  else if (!strcmp(n, "ud2")) {
    e.terminal = 1;
    e.may_trap = 1;
    e.effects = NT_X64_EFFECT_NO_RETURN;
  } else if (member(n, "cli|sti"))
    e.effects = NT_X64_EFFECT_INTERRUPT_STATE;
  else if (!strcmp(n, "hlt"))
    e.effects = NT_X64_EFFECT_SUSPENDS;
  else if (!strcmp(n, "swapgs")) {
    e.effects = NT_X64_EFFECT_CONTROL;
    e.registers_read = e.registers_written = UINT64_C(1) << 18;
  } else if (member(n, "rdmsr|wrmsr")) {
    e.effects = NT_X64_EFFECT_MSR;
    e.registers_read = 2;
    if (!strcmp(n, "rdmsr"))
      e.registers_written = 5;
    else
      e.registers_read |= 5;
  } else if (!strcmp(n, "syscall")) {
    e.effects = NT_X64_EFFECT_CONTROL;
    e.registers_written = (UINT64_C(1) << 1) | (UINT64_C(1) << 11);
    e.registers_read = UINT64_C(1) << 17;
    e.flags_written = NT_X64_FLAGS_ARITH;
  } else if (!strcmp(n, "sysretq")) {
    e.effects = NT_X64_EFFECT_CONTROL | NT_X64_EFFECT_NO_RETURN;
    e.registers_read = (UINT64_C(1) << 1) | (UINT64_C(1) << 11);
    e.terminal = 1;
    e.flags_written = NT_X64_FLAGS_ARITH;
  } else if (!strcmp(n, "iretq")) {
    e.effects = NT_X64_EFFECT_CONTROL | NT_X64_EFFECT_NO_RETURN;
    e.registers_read = e.registers_written = UINT64_C(1) << 4;
    e.stack_read = 1;
    e.flags_written = NT_X64_FLAGS_ARITH;
    e.terminal = 1;
  } else if (member(n, "read_cr|write_cr")) {
    e.effects = NT_X64_EFFECT_CONTROL;
    e.max_cpl = 0;
    if (!strcmp(n, "read_cr"))
      write_operand(&e, &o[0]);
    else
      read_operand(&e, &o[1]);
  } else if (member(n, "sgdt|sidt|lgdt|lidt|ltr|invlpg")) {
    e.effects = NT_X64_EFFECT_CONTROL;
    e.max_cpl = 0;
    if (member(n, "sgdt|sidt"))
      write_operand(&e, &o[0]);
    else if (strcmp(n, "invlpg"))
      read_operand(&e, &o[0]);
  } else if (member(n, "in|out")) {
    e.effects = NT_X64_EFFECT_IO_PORT;
    e.max_cpl = 0;
    if (!strcmp(n, "in")) {
      write_operand(&e, &o[0]);
      read_operand(&e, &o[1]);
    } else {
      read_operand(&e, &o[0]);
      read_operand(&e, &o[1]);
    }
  } else if (!member(n, "nop|pause"))
    return NT_X64_UNKNOWN;
  if (e.flags_read)
    e.registers_read |= UINT64_C(1) << 17;
  if (e.flags_written | e.flags_undefined) {
    e.registers_written |= UINT64_C(1) << 17;
    e.effects |= NT_X64_EFFECT_CHANGES_FLAGS;
  }
  if (!e.max_cpl)
    e.effects |= NT_X64_EFFECT_PRIVILEGED;
  *out = e;
  return NT_X64_OK;
}
