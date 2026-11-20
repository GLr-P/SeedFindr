#include "sfpng.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t crcTable[256];

static void crcInit(void)
{
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crcTable[n] = c;
    }
}

static uint32_t crc(uint32_t c, const unsigned char *b, size_t n)
{
    c ^= 0xFFFFFFFFu;
    while (n--) c = crcTable[(c ^ *b++) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static void be32(unsigned char *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

static void chunk(FILE *f, const char *type, const unsigned char *data, uint32_t len)
{
    unsigned char hdr[8];
    be32(hdr, len);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    if (len) fwrite(data, 1, len, f);
    uint32_t c = crc(0, (const unsigned char *)type, 4);
    c = crc(c, data, len);
    unsigned char t[4];
    be32(t, c);
    fwrite(t, 1, 4, f);
}

// Writes an RGB image as PNG using uncompressed (stored) deflate blocks: simple, dependency-free, a bit larger.
int sfWritePng(const char *path, const unsigned char *rgb, int w, int h)
{
    crcInit();
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    static const unsigned char sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    fwrite(sig, 1, 8, f);
    unsigned char ihdr[13];
    be32(ihdr, w); be32(ihdr + 4, h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);

    size_t raw = (size_t)h * (1 + (size_t)w * 3);
    size_t blocks = (raw + 65534) / 65535;
    size_t zlen = 2 + raw + blocks * 5 + 4;
    unsigned char *z = malloc(zlen), *p = z;
    *p++ = 0x78; *p++ = 0x01;
    uint32_t a = 1, b = 0;
    size_t left = raw, row = 0, col = 0; // position in the raw (filter byte + pixels) stream
    while (left) {
        uint16_t n = left > 65535 ? 65535 : (uint16_t)left;
        left -= n;
        *p++ = left ? 0 : 1;
        *p++ = n & 0xFF; *p++ = n >> 8; *p++ = ~n & 0xFF; *p++ = (~n >> 8) & 0xFF;
        for (uint16_t i = 0; i < n; i++) {
            unsigned char v = col == 0 ? 0 : rgb[row * (size_t)w * 3 + col - 1];
            if (++col == 1 + (size_t)w * 3) { col = 0; row++; }
            *p++ = v;
            a = (a + v) % 65521; b = (b + a) % 65521;
        }
    }
    be32(p, (b << 16) | a); p += 4;
    chunk(f, "IDAT", z, (uint32_t)(p - z));
    chunk(f, "IEND", NULL, 0);
    free(z);
    return fclose(f) == 0;
}
