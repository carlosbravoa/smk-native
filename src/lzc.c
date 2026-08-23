/* Super Mario Kart's graphics codec.
 *
 * Reverse engineered from the decompressors at $84E09E (writes WRAM bank $7F)
 * and $84DF38 (bank $7E); the two routines are byte-identical apart from the
 * destination bank.  See docs/FINDINGS.md.
 *
 *   byte0 == $FF              end of stream
 *   (byte0 & $E0) != $E0      cmd = byte0 >> 5,       len = (byte0 & $1F) + 1
 *   (byte0 & $E0) == $E0      cmd = (byte0 >> 2) & 7, len = (((byte0 & 3) << 8)
 *                                                            | byte1) + 1
 */
#include "smk.h"

enum { CMD_LITERAL, CMD_BYTE_FILL, CMD_WORD_FILL, CMD_INC_FILL,
       CMD_COPY_ABS, CMD_COPY_ABS_INV, CMD_COPY_REL, CMD_COPY_REL_INV };

long smk_decompress(const uint8_t *src, size_t srclen, size_t off,
                    uint8_t *out, size_t outcap, size_t *consumed)
{
    size_t p = off, n = 0;

    for (;;) {
        if (p >= srclen) return -1;
        uint8_t b0 = src[p];
        if (b0 == 0xFF) { p++; break; }

        unsigned cmd, len;
        if ((b0 & 0xE0) != 0xE0) {
            cmd = b0 >> 5;
            len = (b0 & 0x1F) + 1u;
            p += 1;
        } else {
            if (p + 1 >= srclen) return -1;
            cmd = (b0 >> 2) & 7u;
            len = ((((unsigned)b0 & 3u) << 8) | src[p + 1]) + 1u;
            p += 2;
        }
        if (n + len > outcap) return -1;

        switch (cmd) {
        case CMD_LITERAL:
            if (p + len > srclen) return -1;
            for (unsigned i = 0; i < len; i++) out[n + i] = src[p + i];
            p += len;
            break;

        case CMD_BYTE_FILL: {
            if (p >= srclen) return -1;
            uint8_t v = src[p++];
            for (unsigned i = 0; i < len; i++) out[n + i] = v;
            break;
        }
        case CMD_WORD_FILL: {
            if (p + 1 >= srclen) return -1;
            uint8_t a = src[p], b = src[p + 1];
            p += 2;
            for (unsigned i = 0; i < len; i++) out[n + i] = (i & 1) ? b : a;
            break;
        }
        case CMD_INC_FILL: {
            if (p >= srclen) return -1;
            uint8_t v = src[p++];
            for (unsigned i = 0; i < len; i++) out[n + i] = (uint8_t)(v + i);
            break;
        }
        case CMD_COPY_ABS:
        case CMD_COPY_ABS_INV: {
            if (p + 1 >= srclen) return -1;
            size_t s = (size_t)src[p] | ((size_t)src[p + 1] << 8);
            p += 2;
            if (s >= n) return -1;            /* would read uninitialised output */
            uint8_t mask = (cmd == CMD_COPY_ABS_INV) ? 0xFF : 0x00;
            /* byte at a time: an overlapping run repeats, and the game
             * depends on that */
            for (unsigned i = 0; i < len; i++) out[n + i] = out[s + i] ^ mask;
            break;
        }
        case CMD_COPY_REL:
        case CMD_COPY_REL_INV: {
            if (p >= srclen) return -1;
            size_t dist = src[p++];
            if (dist == 0 || dist > n) return -1;
            size_t s = n - dist;
            uint8_t mask = (cmd == CMD_COPY_REL_INV) ? 0xFF : 0x00;
            for (unsigned i = 0; i < len; i++) out[n + i] = out[s + i] ^ mask;
            break;
        }
        default:
            return -1;
        }
        n += len;
    }

    if (consumed) *consumed = p - off;
    return (long)n;
}
