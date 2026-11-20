#include "sfctx.h"
#include "sfterrain.h"
#include <math.h>
#include <string.h>

void ctxInit(SeedCtx *c, int mc)
{
    memset(c, 0, sizeof *c);
    c->mc = mc;
    setupGenerator(&c->ow, mc, 0);
    setupGenerator(&c->nether, mc, 0);
    if (mc >= MC_1_14) setupTerrainNoise(&c->tn, mc, 0);
}

void ctxSetSeed(SeedCtx *c, uint64_t seed)
{
    c->seed = seed;
    c->s48 = seed & MASK48;
    if (c->s48Ready != c->s48 + 1) {
        c->s48Ready = c->s48 + 1;
        for (int i = 0; i < 6; i++) c->endHeights[i] = -2;
        c->endReady = 0;
        c->strongholdApprox = initFirstStronghold(NULL, c->mc, c->s48);
    }
    c->haveOW = c->haveNether = c->haveTerrain = c->haveSpawn = c->haveSpawnY = 0;
}

int ctxEndHeight(SeedCtx *c, int i)
{
    static const int pts[6][2] = { {0, 0}, {40, 0}, {0, 40}, {-40, 0}, {0, -40}, {24, 24} };
    if (c->endHeights[i] != -2) return c->endHeights[i];
    if (!c->endReady) {
        setEndSeed(&c->en, c->mc, c->s48);
        initSurfaceNoise(&c->endSn, DIM_END, c->s48);
        c->endReady = 1;
    }
    // Same as the fork's getEndSurfaceHeight, without rebuilding the noise for every point.
    int x = pts[i][0], z = pts[i][1], cx = x >> 3, cz = z >> 3;
    enum { Y0 = 0, Y1 = 32, YN = Y1 - Y0 + 1 };
    double n00[YN], n01[YN], n10[YN], n11[YN];
    sampleNoiseColumnEnd(n00, &c->endSn, &c->en, cx, cz, Y0, Y1);
    sampleNoiseColumnEnd(n01, &c->endSn, &c->en, cx, cz + 1, Y0, Y1);
    sampleNoiseColumnEnd(n10, &c->endSn, &c->en, cx + 1, cz, Y0, Y1);
    sampleNoiseColumnEnd(n11, &c->endSn, &c->en, cx + 1, cz + 1, Y0, Y1);
    return c->endHeights[i] = getSurfaceHeight(n00, n01, n10, n11, Y0, Y1, 4, (x & 7) / 8.0, (z & 7) / 8.0);
}

Generator *ctxOverworld(SeedCtx *c)
{
    if (!c->haveOW) { applySeed(&c->ow, DIM_OVERWORLD, c->seed); c->haveOW = 1; }
    return &c->ow;
}

Generator *ctxNether(SeedCtx *c)
{
    if (!c->haveNether) { applySeed(&c->nether, DIM_NETHER, c->seed); c->haveNether = 1; }
    return &c->nether;
}

TerrainNoise *ctxTerrain(SeedCtx *c)
{
    if (c->mc < MC_1_14) return NULL;
    if (!c->haveTerrain) { initTerrainNoise(&c->tn, c->seed, DIM_OVERWORLD); c->haveTerrain = 1; }
    return &c->tn;
}

Pos ctxSpawn(SeedCtx *c)
{
    if (!c->haveSpawn) {
        Generator *g = ctxOverworld(c);
        uint64_t rng;
        c->spawn = c->exactSpawn ? getSpawn(g) : estimateSpawn(g, &rng);
        c->haveSpawn = 1;
    }
    return c->spawn;
}

int ctxSpawnY(SeedCtx *c)
{
    if (!c->haveSpawnY) {
        Pos s = ctxSpawn(c);
        TerrainNoise *tn = ctxTerrain(c);
        c->spawnY = tn ? sfSolidTop(tn, s.x, s.z) : -1000;
        c->haveSpawnY = 1;
    }
    return c->spawnY;
}

static Generator *genFor(SeedCtx *c, int st)
{
    StructureConfig sc;
    if (!getStructureConfig(st, c->mc, &sc)) return NULL;
    return sc.dim == DIM_NETHER ? ctxNether(c) : ctxOverworld(c);
}

// Visits candidate regions around (x,z) within maxDist; calls back for viable ones nearest-region-first.
int ctxNearest(SeedCtx *c, int st, int x, int z, int maxDist, Pos *out)
{
    StructureConfig sc;
    if (!getStructureConfig(st, c->mc, &sc)) return -1;
    int rb = sc.regionSize * 16;
    int rx0 = (int)floor((double)(x - maxDist) / rb), rx1 = (int)floor((double)(x + maxDist) / rb);
    int rz0 = (int)floor((double)(z - maxDist) / rb), rz1 = (int)floor((double)(z + maxDist) / rb);
    long long best = (long long)maxDist * maxDist + 1;
    Pos bp = {0, 0};
    Generator *g = NULL;
    TerrainNoise *tn = NULL;
    // Cheap pass first: collect attempt positions within range, nearest first (at most 256 candidates).
    Pos cand[256];
    long long cd[256];
    int n = 0;
    for (int rz = rz0; rz <= rz1; rz++)
        for (int rx = rx0; rx <= rx1; rx++) {
            Pos p;
            if (!getStructurePos(st, c->mc, c->seed, rx, rz, &p)) continue;
            long long d = sfDist2(p.x, p.z, x, z);
            if (d >= best) continue;
            if (n == 256) { if (d >= cd[255]) continue; n--; }
            int i = n++;
            while (i > 0 && cd[i - 1] > d) { cand[i] = cand[i - 1]; cd[i] = cd[i - 1]; i--; }
            cand[i] = p; cd[i] = d;
        }
    for (int i = 0; i < n; i++) {
        if (!g) { g = genFor(c, st); tn = (c->mc >= MC_1_18 && (st == Desert_Pyramid || st == Jungle_Temple)) ? ctxTerrain(c) : NULL; }
        if (sfViableStructure(st, g, tn, cand[i].x, cand[i].z)) { best = cd[i]; bp = cand[i]; break; }
    }
    if (best > (long long)maxDist * maxDist) return -1;
    if (out) *out = bp;
    return (int)sqrt((double)best);
}

int ctxCountNear(SeedCtx *c, int st, int x, int z, int dist, int max)
{
    StructureConfig sc;
    if (!getStructureConfig(st, c->mc, &sc)) return 0;
    int rb = sc.regionSize * 16, count = 0;
    int rx0 = (int)floor((double)(x - dist) / rb), rx1 = (int)floor((double)(x + dist) / rb);
    int rz0 = (int)floor((double)(z - dist) / rb), rz1 = (int)floor((double)(z + dist) / rb);
    Generator *g = NULL;
    TerrainNoise *tn = NULL;
    for (int rz = rz0; rz <= rz1 && count < max; rz++)
        for (int rx = rx0; rx <= rx1 && count < max; rx++) {
            Pos p;
            if (!getStructurePos(st, c->mc, c->seed, rx, rz, &p)) continue;
            if (sfDist2(p.x, p.z, x, z) > (long long)dist * dist) continue;
            if (!g) { g = genFor(c, st); tn = (c->mc >= MC_1_18 && (st == Desert_Pyramid || st == Jungle_Temple)) ? ctxTerrain(c) : NULL; }
            if (sfViableStructure(st, g, tn, p.x, p.z)) count++;
        }
    return count;
}
