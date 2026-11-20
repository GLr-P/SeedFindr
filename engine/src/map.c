// map: top-down picture of one seed: biome colours, terrain relief, structure markers and the spawn.
//   map <version> <seed> <out.png> [--size PIXELS] [--scale BLOCKS_PER_PIXEL] [--center X,Z] [--no-relief]
// Prints JSON with the area shown and every marker drawn.
#include "sfctx.h"
#include "sfpng.h"
#include "sfterrain.h"
#include "util.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

typedef struct { int type; unsigned char r, g, b; } Marker;
static const Marker MARKERS[] = {
    { Village, 230, 160, 60 }, { Desert_Pyramid, 250, 220, 120 }, { Jungle_Temple, 60, 200, 60 },
    { Swamp_Hut, 120, 70, 160 }, { Igloo, 240, 250, 255 }, { Shipwreck, 150, 100, 60 }, { Ocean_Ruin, 90, 200, 220 },
    { Treasure, 255, 215, 0 }, { Ruined_Portal, 170, 40, 220 }, { Outpost, 120, 120, 120 }, { Monument, 0, 120, 200 },
    { Mansion, 110, 60, 20 }, { Ancient_City, 30, 50, 60 }, { Trail_Ruins, 200, 130, 90 }, { Trial_Chambers, 210, 120, 40 },
};

static unsigned char *img;
static int gMc, gSize, gScale, gBx0, gBz0, gStep, gGw, gThreads, *gIds, *gHgt;
static uint64_t gSeed;

// Each thread renders a strip of biome rows and relief-grid rows with its own generator.
static DWORD WINAPI strip(LPVOID arg)
{
    int t = (int)(intptr_t)arg;
    SeedCtx *c = malloc(sizeof *c);
    ctxInit(c, gMc);
    ctxSetSeed(c, gSeed);
    Generator *g = ctxOverworld(c);
    int r0 = gSize * t / gThreads, r1 = gSize * (t + 1) / gThreads;
    if (r1 > r0) {
        Range r = { gScale, gBx0 / gScale, gBz0 / gScale + r0, gSize, r1 - r0, gScale == 1 ? 64 : 16, 1 };
        int *ids = allocCache(g, r);
        genBiomes(g, ids, r);
        memcpy(gIds + (size_t)r0 * gSize, ids, sizeof(int) * gSize * (r1 - r0));
        free(ids);
    }
    TerrainNoise *tn = gHgt ? ctxTerrain(c) : NULL;
    if (tn)
        for (int j = gGw * t / gThreads; j < gGw * (t + 1) / gThreads; j++)
            for (int i = 0; i < gGw; i++) gHgt[j * gGw + i] = sfSolidTop(tn, gBx0 + i * gStep * gScale, gBz0 + j * gStep * gScale);
    free(c);
    return 0;
}
static int W, H;

static void px(int x, int y, int r, int g, int b)
{
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    unsigned char *p = img + ((size_t)y * W + x) * 3;
    p[0] = r; p[1] = g; p[2] = b;
}

static void box(int cx, int cy, int half, int r, int g, int b)
{
    for (int y = -half - 1; y <= half + 1; y++)
        for (int x = -half - 1; x <= half + 1; x++) {
            int edge = abs(x) == half + 1 || abs(y) == half + 1;
            if (edge) px(cx + x, cy + y, 0, 0, 0); else px(cx + x, cy + y, r, g, b);
        }
}

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: map <version> <seed> <out.png> [--size N] [--scale S] [--center X,Z] [--no-relief]\n"); return 2; }
    int mc = str2mc(argv[1]);
    uint64_t seed = (uint64_t)strtoll(argv[2], NULL, 10);
    const char *out = argv[3];
    int size = 512, scale = 4, relief = 1, haveCenter = 0, cx = 0, cz = 0;
    for (int i = 4; i < argc; i++) {
        if (!strcmp(argv[i], "--size") && i + 1 < argc) size = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--center") && i + 1 < argc) { haveCenter = sscanf(argv[++i], "%d,%d", &cx, &cz) == 2; }
        else if (!strcmp(argv[i], "--no-relief")) relief = 0;
    }
    if (mc <= 0 || size < 16 || size > 4096 || (scale != 1 && scale != 4 && scale != 16 && scale != 64)) {
        fprintf(stderr, "bad arguments\n");
        return 2;
    }
    SeedCtx c;
    ctxInit(&c, mc);
    c.exactSpawn = 1;
    ctxSetSeed(&c, seed);
    Pos spawn = ctxSpawn(&c);
    if (!haveCenter) { cx = spawn.x; cz = spawn.z; }
    W = H = size;
    int bx0 = cx - size * scale / 2, bz0 = cz - size * scale / 2; // block coords of the top-left pixel

    // Biomes at the surface (1.18+ biomes are 3D: sample at y=64 so caves don't show through) and relief heights,
    // rendered in strips on every CPU thread.
    Generator *g = ctxOverworld(&c);
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    gThreads = si.dwNumberOfProcessors > 2 ? (int)si.dwNumberOfProcessors - 1 : 1;
    if (gThreads > 64) gThreads = 64;
    gMc = mc; gSeed = seed; gSize = size; gScale = scale; gBx0 = bx0; gBz0 = bz0; gStep = 4; gGw = size / 4 + 2;
    gIds = malloc(sizeof(int) * size * size);
    gHgt = relief && mc >= MC_1_14 ? malloc(sizeof(int) * gGw * gGw) : NULL;
    HANDLE th[64];
    for (int t = 0; t < gThreads; t++) th[t] = CreateThread(NULL, 0, strip, (LPVOID)(intptr_t)t, 0, NULL);
    WaitForMultipleObjects(gThreads, th, TRUE, INFINITE);
    unsigned char colors[256][3];
    initBiomeColors(colors);
    img = malloc((size_t)W * H * 3);
    biomesToImage(img, colors, gIds, size, size, 1, 0);
    free(gIds);

    // Relief: heights on a coarse grid, then hill shading (light from the north-west).
    if (gHgt) {
        int step = gStep, gw = gGw;
        int *hgt = gHgt;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                double fx = (double)x / step, fz = (double)y / step;
                int i = (int)fx, j = (int)fz;
                double tx = fx - i, tz = fz - j;
                double h00 = hgt[j * gw + i], h10 = hgt[j * gw + i + 1], h01 = hgt[(j + 1) * gw + i], h11 = hgt[(j + 1) * gw + i + 1];
                double dhx = (h10 - h00) * (1 - tz) + (h11 - h01) * tz, dhz = (h01 - h00) * (1 - tx) + (h11 - h10) * tx;
                double shade = 1.0 - 0.35 * tanh((dhx + dhz) / (step * scale) * 2.0);
                double hh = h00 * (1 - tx) * (1 - tz) + h10 * tx * (1 - tz) + h01 * (1 - tx) * tz + h11 * tx * tz;
                if (hh < 62) shade *= 0.75 + 0.25 * fmax(0, (hh - 20) / 42.0); // deeper water darker
                unsigned char *p = img + ((size_t)y * W + x) * 3;
                for (int k = 0; k < 3; k++) { double v = p[k] * shade; p[k] = v > 255 ? 255 : v < 0 ? 0 : (unsigned char)v; }
            }
        free(hgt);
    }

    // Structure markers.
    printf("{\"version\":\"%s\",\"seed\":%lld,\"png\":\"%s\",\"area\":{\"x\":%d,\"z\":%d,\"width\":%d,\"scale\":%d},"
        "\"spawn\":[%d,%d],\"markers\":[", mc2str(mc), (long long)seed, out, bx0, bz0, size * scale, scale, spawn.x, spawn.z);
    int first = 1, half = size >= 384 ? 3 : 2;
    for (size_t m = 0; m < sizeof MARKERS / sizeof *MARKERS; m++) {
        StructureConfig sc;
        if (!getStructureConfig(MARKERS[m].type, mc, &sc) || sc.dim != DIM_OVERWORLD) continue;
        int rb = sc.regionSize * 16;
        int rx0 = (int)floor((double)bx0 / rb) - 1, rx1 = (int)floor((double)(bx0 + size * scale) / rb);
        int rz0 = (int)floor((double)bz0 / rb) - 1, rz1 = (int)floor((double)(bz0 + size * scale) / rb);
        for (int rz = rz0; rz <= rz1; rz++)
            for (int rx = rx0; rx <= rx1; rx++) {
                Pos p;
                if (!getStructurePos(MARKERS[m].type, mc, seed, rx, rz, &p)) continue;
                int ix = (p.x - bx0) / scale, iz = (p.z - bz0) / scale;
                if (ix < 0 || iz < 0 || ix >= W || iz >= H) continue;
                if (!sfViableStructure(MARKERS[m].type, g, c.mc >= MC_1_18 ? ctxTerrain(&c) : NULL, p.x, p.z)) continue;
                box(ix, iz, half, MARKERS[m].r, MARKERS[m].g, MARKERS[m].b);
                printf("%s{\"type\":\"%s\",\"pos\":[%d,%d]}", first ? "" : ",", struct2str(MARKERS[m].type), p.x, p.z);
                first = 0;
            }
    }
    // Spawn: white cross with a black outline.
    int sx = (spawn.x - bx0) / scale, sz = (spawn.z - bz0) / scale;
    for (int d = -7; d <= 7; d++)
        for (int t = -2; t <= 2; t++) { px(sx + d, sz + t, 0, 0, 0); px(sx + t, sz + d, 0, 0, 0); }
    for (int d = -6; d <= 6; d++)
        for (int t = -1; t <= 1; t++) { px(sx + d, sz + t, 255, 255, 255); px(sx + t, sz + d, 255, 255, 255); }
    printf("]}\n");
    int ok = sfWritePng(out, img, W, H);
    free(img);
    return ok ? 0 : 1;
}
