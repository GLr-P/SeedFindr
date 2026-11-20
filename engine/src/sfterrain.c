#include "sfterrain.h"

static int floordiv4(int v) { return v >> 2; } // arithmetic shift = floor division for negatives

int sfSolidTop(TerrainNoise *tn, int x, int z)
{
    enum { COLMIN = 0, COLMAX = 48, N = COLMAX - COLMIN + 1 };
    double ds00[N], ds01[N], ds10[N], ds11[N];
    int cx = floordiv4(x), cz = floordiv4(z);
    sampleNoiseColumn(tn, cx, cz, COLMIN, COLMAX, ds00);
    sampleNoiseColumn(tn, cx, cz + 1, COLMIN, COLMAX, ds01);
    sampleNoiseColumn(tn, cx + 1, cz, COLMIN, COLMAX, ds10);
    sampleNoiseColumn(tn, cx + 1, cz + 1, COLMIN, COLMAX, ds11);
    // With the flag set, generateColumn stops at the first solid block from the top and returns the air block above it.
    int airAbove = generateColumn(NULL, ds00, ds01, ds10, ds11, COLMIN, COLMAX, 8, (x & 3) / 4.0, (z & 3) / 4.0,
        INTERP_1_18, -64, 1);
    return airAbove - 1;
}

int sfViableStructure(int st, Generator *g, TerrainNoise *tn, int x, int z)
{
    if (!isViableStructurePos(st, g, x, z, 0)) return 0;
    if (g->mc < MC_1_18 || !tn || (st != Desert_Pyramid && st != Jungle_Temple)) return 1;
    int w = st == Desert_Pyramid ? 21 : 12, d = st == Desert_Pyramid ? 21 : 15;
    int x0 = x & ~15, z0 = z & ~15; // chunk minimum corner
    const int corners[4][2] = { {x0, z0}, {x0, z0 + d}, {x0 + w, z0}, {x0 + w, z0 + d} };
    for (int i = 0; i < 4; i++)
        if (sfSolidTop(tn, corners[i][0], corners[i][1]) < 63) return 0;
    return 1;
}
