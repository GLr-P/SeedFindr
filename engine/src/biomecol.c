// biomecol: biome by height at one block column, plus the terrain surface estimate (debugging biome checks).
//   biomecol <version> <seed> <x> <z>
#include "finders.h"
#include "sfterrain.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "usage: biomecol <version> <seed> <x> <z>\n"); return 2; }
    int mc = str2mc(argv[1]);
    uint64_t seed = (uint64_t)strtoll(argv[2], NULL, 10);
    int x = atoi(argv[3]), z = atoi(argv[4]);
    Generator g;
    setupGenerator(&g, mc, 0);
    applySeed(&g, DIM_OVERWORLD, seed);
    int surf = -999;
    if (mc >= MC_1_18) {
        TerrainNoise tn;
        setupTerrainNoise(&tn, mc, 0);
        initTerrainNoise(&tn, seed, DIM_OVERWORLD);
        surf = samplePreliminarySurfaceLevel(&tn, x, z);
        printf("{\"solidTop\": %d, ", sfSolidTop(&tn, x, z));
    } else printf("{");
    printf("\"x\": %d, \"z\": %d, \"preliminarySurface\": %d, \"byY\": {", x, z, surf);
    int first = 1, last = -1;
    for (int y = 320; y >= -64; y -= 4) {
        int id = getBiomeAt(&g, 4, x >> 2, y >> 2, z >> 2);
        if (id != last || y == -64) { printf("%s\"%d\": \"%s\"", first ? "" : ", ", y, biome2str(mc, id)); first = 0; last = id; }
    }
    printf("}}\n");
    return 0;
}
