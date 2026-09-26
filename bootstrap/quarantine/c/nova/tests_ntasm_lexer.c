#include "nova.h"
#include "nova/run_host.h"
#include "nova/runtime_v2.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { TK_EOF = 256, TK_NAME, TK_INT, TK_STRING, TK_NL, TK_ARROW,
       TK_EQ, TK_NE, TK_LE, TK_GE, TK_SHL, TK_SHR, TK_LAND, TK_LOR };
typedef struct {
  uint64_t kind, value, source, line, column, start, end, text_start,
      text_length;
} RefToken;
typedef struct {
  uint64_t code, reason, source, line, column, start, end;
} RefDiagnostic;
typedef struct {
  RefToken *tokens;
  size_t count, capacity;
  RefDiagnostic diagnostic;
} Reference;
static unsigned checks, failures, cases;
#define CHECK(c, m)                                                            \
  do {                                                                         \
    checks++;                                                                  \
    if (!(c)) {                                                                \
      failures++;                                                              \
      fprintf(stderr, "FAIL %s\n", m);                                        \
    }                                                                          \
  } while (0)
static int letter(unsigned char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static int digit(unsigned char c) { return c >= '0' && c <= '9'; }
static int xdigit_(unsigned char c) {
  return digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
static unsigned xvalue(unsigned char c) {
  return digit(c) ? (unsigned)(c - '0')
                  : c >= 'a' ? (unsigned)(c - 'a' + 10)
                             : (unsigned)(c - 'A' + 10);
}
static void advance(const unsigned char *s, size_t *cursor, uint64_t *line,
                    uint64_t *column) {
  unsigned char c = s[(*cursor)++];
  if (c == '\n') {
    ++*line;
    *column = 1;
  } else
    ++*column;
}
static int fail_ref(Reference *r, uint64_t reason, uint64_t source,
                    uint64_t line, uint64_t column, size_t start, size_t end) {
  r->diagnostic = (RefDiagnostic){100, reason, source, line, column, start, end};
  return 0;
}
static int push(Reference *r, RefToken token) {
  if (r->count == r->capacity) {
    size_t capacity = r->capacity ? r->capacity * 2 : 16;
    RefToken *tokens = (RefToken *)realloc(r->tokens, capacity * sizeof(*tokens));
    if (!tokens)
      return 0;
    r->tokens = tokens;
    r->capacity = capacity;
  }
  r->tokens[r->count++] = token;
  return 1;
}
static int reference_lex(const unsigned char *s, size_t n, uint64_t source,
                         Reference *r) {
  size_t cursor = 0;
  uint64_t line = 1, column = 1;
  memset(r, 0, sizeof(*r));
  for (;;) {
    while (cursor < n) {
      unsigned char c = s[cursor];
      if (c == ' ' || c == '\t') {
        advance(s, &cursor, &line, &column);
        continue;
      }
      if (c == '\r') {
        if (cursor + 1 >= n || s[cursor + 1] != '\n')
          return fail_ref(r, 1, source, line, column, cursor, cursor + 1);
        advance(s, &cursor, &line, &column);
        continue;
      }
      if (c == '/' && cursor + 1 < n && s[cursor + 1] == '/') {
        while (cursor < n && s[cursor] != '\n')
          advance(s, &cursor, &line, &column);
        continue;
      }
      break;
    }
    RefToken t = {TK_EOF, 0, source, line, column, cursor, cursor, cursor, 0};
    if (cursor >= n)
      return push(r, t);
    unsigned char c = s[cursor];
    if (c == '\n') {
      t.kind = TK_NL;
      advance(s, &cursor, &line, &column);
    } else if (letter(c)) {
      t.kind = TK_NAME;
      do {
        advance(s, &cursor, &line, &column);
      } while (cursor < n && (letter(s[cursor]) || digit(s[cursor])));
    } else if (digit(c)) {
      t.kind = TK_INT;
      unsigned base = 10;
      int previous = 0;
      if (c == '0' && cursor + 1 < n &&
          (s[cursor + 1] == 'x' || s[cursor + 1] == 'X')) {
        base = 16;
        advance(s, &cursor, &line, &column);
        advance(s, &cursor, &line, &column);
        if (cursor >= n || !xdigit_(s[cursor]))
          return fail_ref(r, 2, source, t.line, t.column, t.start, cursor);
      }
      while (cursor < n) {
        unsigned char d = s[cursor];
        if (d == '_') {
          if (!previous || cursor + 1 >= n ||
              !(base == 16 ? xdigit_(s[cursor + 1]) : digit(s[cursor + 1])))
            return fail_ref(r, 3, source, t.line, t.column, t.start, cursor + 1);
          previous = 0;
          advance(s, &cursor, &line, &column);
          continue;
        }
        if (!(base == 16 ? xdigit_(d) : digit(d)))
          break;
        unsigned v = base == 16 ? xvalue(d) : (unsigned)(d - '0');
        if (t.value > (UINT64_MAX - v) / base)
          return fail_ref(r, 4, source, t.line, t.column, t.start, cursor + 1);
        t.value = t.value * base + v;
        previous = 1;
        advance(s, &cursor, &line, &column);
      }
      if (cursor < n && letter(s[cursor])) {
        while (cursor < n && (letter(s[cursor]) || digit(s[cursor])))
          advance(s, &cursor, &line, &column);
        return fail_ref(r, 5, source, t.line, t.column, t.start, cursor);
      }
    } else if (c == '"') {
      t.kind = TK_STRING;
      advance(s, &cursor, &line, &column);
      t.text_start = cursor;
      while (cursor < n && s[cursor] != '"') {
        unsigned char d = s[cursor];
        if (d == '\n' || d == '\r' || d == 0)
          return fail_ref(r, 6, source, t.line, t.column, t.start, cursor + 1);
        if (d == '\\') {
          advance(s, &cursor, &line, &column);
          if (cursor >= n)
            break;
          unsigned char e = s[cursor];
          if (e == 'x') {
            advance(s, &cursor, &line, &column);
            if (cursor + 1 >= n || !xdigit_(s[cursor]) ||
                !xdigit_(s[cursor + 1]))
              return fail_ref(r, 7, source, t.line, t.column, t.start, cursor);
            advance(s, &cursor, &line, &column);
            advance(s, &cursor, &line, &column);
            continue;
          }
          if (!strchr("\\\"nrt0", e))
            return fail_ref(r, 8, source, t.line, t.column, t.start, cursor + 1);
        }
        advance(s, &cursor, &line, &column);
      }
      if (cursor >= n || s[cursor] != '"')
        return fail_ref(r, 9, source, t.line, t.column, t.start, cursor);
      t.text_length = cursor - t.text_start;
      advance(s, &cursor, &line, &column);
    } else {
      advance(s, &cursor, &line, &column);
      t.kind = c;
      if (cursor < n) {
        unsigned char d = s[cursor];
        if (c == '-' && d == '>') t.kind = TK_ARROW;
        else if (c == '=' && d == '=') t.kind = TK_EQ;
        else if (c == '!' && d == '=') t.kind = TK_NE;
        else if (c == '<' && d == '=') t.kind = TK_LE;
        else if (c == '>' && d == '=') t.kind = TK_GE;
        else if (c == '<' && d == '<') t.kind = TK_SHL;
        else if (c == '>' && d == '>') t.kind = TK_SHR;
        else if (c == '&' && d == '&') t.kind = TK_LAND;
        else if (c == '|' && d == '|') t.kind = TK_LOR;
        if (t.kind >= TK_ARROW)
          advance(s, &cursor, &line, &column);
      }
      if (c < 32 || c > 126)
        return fail_ref(r, 10, source, t.line, t.column, t.start, cursor);
    }
    t.end = cursor;
    if (!t.text_length)
      t.text_length = t.end - t.start;
    if (!push(r, t))
      return 0;
  }
}
static char *load(const char *path, size_t *length) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *data = size >= 0 ? (char *)malloc((size_t)size + 1) : NULL;
  if (!data || fread(data, 1, (size_t)size, f) != (size_t)size) {
    free(data);
    fclose(f);
    return NULL;
  }
  fclose(f);
  data[size] = 0;
  *length = (size_t)size;
  return data;
}
static void parity_case(const NovaHostImage *image, const char *name,
                        const unsigned char *source, size_t length) {
  Reference ref;
  int ref_ok = reference_lex(source, length, 7, &ref);
  size_t word_count = 9 + (length + 1) * 9;
  uint64_t *words = (uint64_t *)calloc(word_count, sizeof(*words));
  NovaContextV2 context;
  int setup = words && nova_context_init_v2(&context);
  uint64_t returned = UINT64_MAX;
  if (setup) {
    uint64_t source_handle =
        nova_borrow_v2(&context, (void *)source, (uint64_t)length, 0);
    uint64_t output_handle = nova_borrow_v2(
        &context, words, (uint64_t)(word_count * sizeof(*words)), 1);
    uint64_t args[] = {source_handle, length, 7, output_handle, word_count};
    returned = image->entry(&context, args, 5);
  }
  int same = setup && !context.error && returned == (ref_ok ? 0 : 100) &&
             words[0] == (uint64_t)ref_ok &&
             words[8] == (ref_ok ? ref.count : 0);
  if (same && ref_ok) {
    for (size_t i = 0; i < ref.count && same; i++) {
      const uint64_t *actual = words + 9 + i * 9;
      const uint64_t *expected = (const uint64_t *)&ref.tokens[i];
      for (size_t j = 0; j < 9; j++)
        if (actual[j] != expected[j])
          same = 0;
    }
  } else if (same) {
    const uint64_t expected[] = {
        ref.diagnostic.code,   ref.diagnostic.reason, ref.diagnostic.source,
        ref.diagnostic.line,   ref.diagnostic.column, ref.diagnostic.start,
        ref.diagnostic.end};
    for (size_t i = 0; i < 7; i++)
      if (words[1 + i] != expected[i])
        same = 0;
  }
  checks++;
  cases++;
  if (!same) {
    failures++;
    fprintf(stderr,
            "FAIL parity %s ref_ok=%d return=%llu runtime=%llu count=%zu/%llu\n",
            name, ref_ok, (unsigned long long)returned,
            (unsigned long long)(setup ? context.error : UINT64_MAX), ref.count,
            (unsigned long long)(words ? words[8] : 0));
    if (!ref_ok && words)
      fprintf(stderr,
              " diag expected=%llu,%llu,%llu:%llu:%llu,%llu-%llu "
              "actual=%llu,%llu,%llu:%llu:%llu,%llu-%llu\n",
              (unsigned long long)ref.diagnostic.code,
              (unsigned long long)ref.diagnostic.reason,
              (unsigned long long)ref.diagnostic.source,
              (unsigned long long)ref.diagnostic.line,
              (unsigned long long)ref.diagnostic.column,
              (unsigned long long)ref.diagnostic.start,
              (unsigned long long)ref.diagnostic.end,
              (unsigned long long)words[1], (unsigned long long)words[2],
              (unsigned long long)words[3], (unsigned long long)words[4],
              (unsigned long long)words[5], (unsigned long long)words[6],
              (unsigned long long)words[7]);
    fprintf(stderr, " bytes=");
    for (size_t i = 0; i < length; i++)
      fprintf(stderr, "%02x", source[i]);
    fprintf(stderr, "\n");
  }
  if (setup)
    CHECK(nova_context_destroy_v2(&context), "lexer context cleanup");
  free(words);
  free(ref.tokens);
}
static void limit_case(const NovaHostImage *image, const char *name,
                       unsigned char *source, size_t length, uint64_t words_len,
                       uint64_t returned_expected, uint64_t reason,
                       uint64_t count_expected) {
  uint64_t *words = (uint64_t *)calloc((size_t)words_len, sizeof(*words));
  NovaContextV2 context;
  int setup = words && nova_context_init_v2(&context);
  uint64_t returned = UINT64_MAX;
  if (setup) {
    uint64_t source_handle =
        nova_borrow_v2(&context, source, (uint64_t)length, 0);
    uint64_t output_handle = nova_borrow_v2(
        &context, words, words_len * (uint64_t)sizeof(*words), 1);
    uint64_t args[] = {source_handle, length, 9, output_handle, words_len};
    returned = image->entry(&context, args, 5);
  }
  int ok = setup && !context.error && returned == returned_expected &&
           words[1] == returned_expected && words[2] == reason &&
           words[8] == count_expected;
  if (!returned_expected)
    ok = setup && !context.error && !returned && words[0] == 1 &&
         words[1] == 0 && words[8] == count_expected;
  checks++;
  cases++;
  if (!ok) {
    failures++;
    fprintf(stderr, "FAIL limit %s return=%llu reason=%llu count=%llu\n",
            name, (unsigned long long)returned,
            (unsigned long long)(words ? words[2] : 0),
            (unsigned long long)(words ? words[8] : 0));
  }
  if (setup)
    CHECK(nova_context_destroy_v2(&context), "limit context cleanup");
  free(words);
}
int main(void) {
  const char *base = "toolchain/ntasm-nova/";
  char lexer_path[256], entry_path[256];
  snprintf(lexer_path, sizeof lexer_path, "%slexer.nova", base);
  snprintf(entry_path, sizeof entry_path, "%stests/lexer-entry.nova", base);
  size_t lexer_length = 0, entry_length = 0;
  char *lexer = load(lexer_path, &lexer_length);
  char *entry = load(entry_path, &entry_length);
  CHECK(lexer && entry, "lexer Nova sources load");
  NtFInput inputs[] = {{"lexer-entry.nova", entry, entry_length},
                       {"lexer.nova", lexer, lexer_length}};
  NtArtifact artifact;
  int compiled = lexer && entry && nova_compile_many_v2(inputs, 2, &artifact);
  CHECK(compiled, "Nova lexer modules compile");
  if (!compiled && lexer && entry)
    fprintf(stderr, "E%u %zu:%zu %s\n", artifact.error.code,
            artifact.error.line, artifact.error.column, artifact.error.message);
  NovaHostImage image = {0};
  CHECK(compiled && nova_host_map(&artifact, &image), "Nova lexer image maps");
  if (image.memory) {
    static const unsigned char empty[] = "";
    static const unsigned char ordinary[] =
        "_Name9 0 42 0x2A 18_446_744_073_709_551_615 -> == != <= >= << "
        ">> && || & |\n\"A\\n\\x42\\0\\\"\\\\\" // tail\r\nnext";
    static const unsigned char punctuation[] =
        "(){}[]:;,.+-*/%&&||&|^~!<>=@$\n";
    static const unsigned char isolated_cr[] = {'a', '\r', 'b'};
    static const unsigned char bad_string_byte[] = {'"', 'a', '\n'};
    static const unsigned char invalid_byte[] = {0x80};
    parity_case(&image, "empty", empty, 0);
    parity_case(&image, "ordinary", ordinary, sizeof ordinary - 1);
    parity_case(&image, "punctuation", punctuation, sizeof punctuation - 1);
    parity_case(&image, "empty_string", (const unsigned char *)"\"\"", 2);
    parity_case(&image, "isolated_cr", isolated_cr, sizeof isolated_cr);
    parity_case(&image, "hex_digits", (const unsigned char *)"0x", 2);
    parity_case(&image, "underscore", (const unsigned char *)"1_", 2);
    parity_case(&image, "overflow",
                (const unsigned char *)"18446744073709551616", 20);
    parity_case(&image, "suffix", (const unsigned char *)"12abc", 5);
    parity_case(&image, "string_byte", bad_string_byte,
                sizeof bad_string_byte);
    parity_case(&image, "hex_escape", (const unsigned char *)"\"\\xG0\"", 6);
    parity_case(&image, "unknown_escape", (const unsigned char *)"\"\\q\"", 4);
    parity_case(&image, "unterminated", (const unsigned char *)"\"abc", 4);
    parity_case(&image, "invalid_byte", invalid_byte, sizeof invalid_byte);
    size_t fixture_length = 0;
    char *fixture = load("toolchain/ntasm-nova/tests/fixtures/gp-error-witness.ntasm",
                         &fixture_length);
    CHECK(fixture != NULL, "real NTASM fixture loads");
    if (fixture)
      parity_case(&image, "real_gp_source", (unsigned char *)fixture,
                  fixture_length);
    free(fixture);
    static const char *fixture_paths[] = {
        "toolchain/ntasm-nova/tests/fixtures/positive-module.ntasm",
        "toolchain/ntasm-nova/tests/fixtures/negative-isolated-cr.ntasm",
        "toolchain/ntasm-nova/tests/fixtures/negative-unterminated-string.ntasm",
        "toolchain/ntasm-nova/tests/fixtures/negative-overflow.ntasm"};
    for (size_t fixture_index = 0;
         fixture_index < sizeof fixture_paths / sizeof fixture_paths[0];
         fixture_index++) {
      fixture = load(fixture_paths[fixture_index], &fixture_length);
      CHECK(fixture != NULL, "owned lexer fixture loads");
      if (fixture)
        parity_case(&image, fixture_paths[fixture_index],
                    (unsigned char *)fixture, fixture_length);
      free(fixture);
    }
    static const unsigned char alphabet[] =
        "abcXYZ_019x /\t\r\n\"\\+-<>=!;[]{}\x00\x80";
    uint64_t random = UINT64_C(0x9e3779b97f4a7c15);
    for (unsigned sample = 0; sample < 128; sample++) {
      unsigned char bytes[64];
      random ^= random << 7;
      random ^= random >> 9;
      size_t amount = (size_t)(random & 63);
      for (size_t i = 0; i < amount; i++) {
        random ^= random << 7;
        random ^= random >> 9;
        bytes[i] = alphabet[random % (sizeof alphabet - 1)];
      }
      char name[32];
      snprintf(name, sizeof name, "random_%u", sample);
      parity_case(&image, name, bytes, amount);
    }
    size_t source_limit = 4u * 1024u * 1024u;
    unsigned char *large = (unsigned char *)malloc(source_limit + 1);
    CHECK(large != NULL, "large source corpus allocation");
    if (large) {
      memset(large, ' ', source_limit + 1);
      limit_case(&image, "source_exact", large, source_limit, 18, 0, 0, 1);
      limit_case(&image, "source_plus_one", large, source_limit + 1, 9, 201,
                 11, 0);
      memset(large, ';', 262143);
      uint64_t exact_count = 262143;
      uint64_t exact_words = 9 + exact_count * 9;
      limit_case(&image, "tokens_exact", large, 262142, exact_words, 0, 0,
                 exact_count);
      limit_case(&image, "tokens_plus_one", large, 262143, 9, 201, 12, 0);
    }
    free(large);
  }
  if (image.memory)
    nova_host_unmap(&image);
  if (compiled)
    nt_artifact_free(&artifact);
  free(lexer);
  free(entry);
  printf("NTASM_NOVA_LEXER_PARITY checks=%u failures=%u cases=%u\n", checks,
         failures, cases);
  return failures ? 1 : 0;
}
