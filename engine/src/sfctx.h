// Per-thread, per-seed context with lazily computed (and cached) world data, so detectors never repeat work.
#ifndef SFCTX_H
#define SFCTX_H

#include "finders.h"
#include "terrainnoise.h"

#define MASK48 ((1ULL << 48) - 1)

typedef struct {
    int mc;
    uint64_t seed;        // full 64-bit world seed
    uint64_t s48;         // lower 48 bits: the structure seed
    int exactSpawn;       // 0 = estimateSpawn (fast, funnel), 1 = getSpawn (exact, for reported hits)

    // --- per structure seed (kept while only the upper 16 bits change) ---
    uint64_t s48Ready;    // s48 the cached values below belong to (+1 so 0 means none)
    int endHeights[6];    // End surface height at fixed points around 0,0 (-2 = not computed yet)
    int endReady;         // End noise set up for this structure seed
    EndNoise en;
    SurfaceNoise endSn;
    Pos strongholdApprox; // first stronghold, approximate (48-bit)

    // --- per world seed ---
    int haveOW, haveNether, haveTerrain, haveSpawn, haveSpawnY;
    Generator ow, nether;
    TerrainNoise tn;
    Pos spawn;
    int spawnY;
} SeedCtx;

void ctxInit(SeedCtx *c, int mc);
// Switch to a new world seed; structure-seed caches survive when the lower 48 bits stay the same.
void ctxSetSeed(SeedCtx *c, uint64_t seed);

int ctxEndHeight(SeedCtx *c, int i);     // End surface height at point i (0 = centre, 1-4 ring at 40, 5 = 24,24), cached per structure seed
Generator *ctxOverworld(SeedCtx *c);
Generator *ctxNether(SeedCtx *c);
TerrainNoise *ctxTerrain(SeedCtx *c);    // NULL for versions without block terrain in the fork (< 1.14)
Pos ctxSpawn(SeedCtx *c);
int ctxSpawnY(SeedCtx *c);               // highest solid noise block at spawn (no trees/water)

// Nearest viable structure of a type to (x,z) within maxDist blocks. Returns distance (blocks) or -1.
int ctxNearest(SeedCtx *c, int structType, int x, int z, int maxDist, Pos *out);
// Number of viable structures of a type within dist blocks of (x,z) (stops counting at max).
int ctxCountNear(SeedCtx *c, int structType, int x, int z, int dist, int max);

static inline long long sfDist2(int ax, int az, int bx, int bz)
{
    long long dx = ax - bx, dz = az - bz;
    return dx * dx + dz * dz;
}

#endif
