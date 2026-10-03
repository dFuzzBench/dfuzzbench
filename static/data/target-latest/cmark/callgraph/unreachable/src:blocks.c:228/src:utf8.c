#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

#include "cmark_ctype.h"
#include "utf8.h"

static const int8_t utf8proc_utf8class[256] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    4, 4, 4, 4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0};

static void encode_unknown(cmark_strbuf *buf) {
  static const uint8_t repl[] = {239, 191, 189};
  cmark_strbuf_put(buf, repl, 3);
}

static int utf8proc_charlen(const uint8_t *str, bufsize_t str_len) {
  int length, i;

  if (!str_len)
    return 0;

  length = utf8proc_utf8class[str[0]];

  if (!length)
    return -1;

  if (str_len >= 0 && (bufsize_t)length > str_len)
    return -str_len;

  for (i = 1; i < length; i++) {
    if ((str[i] & 0xC0) != 0x80)
      return -i;
  }

  return length;
}

// Validate a single UTF-8 character according to RFC 3629.
static int utf8proc_valid(const uint8_t *str, bufsize_t str_len) {
  int length = utf8proc_utf8class[str[0]];

  if (!length)
    return -1;

  if ((bufsize_t)length > str_len)
    return -str_len;

  switch (length) {
  case 2:
    if ((str[1] & 0xC0) != 0x80)
      return -1;
    if (str[0] < 0xC2) {
      // Overlong
      return -length;
    }
    break;

  case 3:
    if ((str[1] & 0xC0) != 0x80)
      return -1;
    if ((str[2] & 0xC0) != 0x80)
      return -2;
    if (str[0] == 0xE0) {
      if (str[1] < 0xA0) {
        // Overlong
        return -length;
      }
    } else if (str[0] == 0xED) {
      if (str[1] >= 0xA0) {
        // Surrogate
        return -length;
      }
    }
    break;

  case 4:
    if ((str[1] & 0xC0) != 0x80)
      return -1;
    if ((str[2] & 0xC0) != 0x80)
      return -2;
    if ((str[3] & 0xC0) != 0x80)
      return -3;
    if (str[0] == 0xF0) {
      if (str[1] < 0x90) {
        // Overlong
        return -length;
      }
    } else if (str[0] >= 0xF4) {
      if (str[0] > 0xF4 || str[1] >= 0x90) {
        // Above 0x10FFFF
        return -length;
      }
    }
    break;
  }

  return length;
}

void cmark_utf8proc_check(cmark_strbuf *ob, const uint8_t *line,
                          bufsize_t size) {
  bufsize_t i = 0;

  while (i < size) {
    bufsize_t org = i;
    int charlen = 0;

    while (i < size) {
      if (line[i] < 0x80 && line[i] != 0) {
        i++;
      } else if (line[i] >= 0x80) {
        charlen = utf8proc_valid(line + i, size - i);
        if (charlen < 0) {
          charlen = -charlen;
          break;
        }
        i += charlen;
      } else if (line[i] == 0) {
        // 