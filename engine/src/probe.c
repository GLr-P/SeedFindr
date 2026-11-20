// probe: prints what the engine sees for one seed, and a quick speed benchmark.
//   probe <version> <seed>      JSON: spawn, nearby structures, strongholds, End centre
//   probe bench <version> [s]   seeds/second for stage 1 (structure position) and stage 2 (biome check)
#include "finders.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const int OVERWORLD_TYPES[] = { Village, Desert_Pyramid, Jungle_Temple, Swamp_Hut, Igloo, Shipwreck,
    Ocean_Ruin, Treasure, Ruined_Portal, Outpost, Monument, Mansion };
static const int NETHER_TYPES[] = { Fortress, Bastion };

static double now(void) { return (double)clock() / CLOCKS_PER_SEC; }

static long long dist2(Pos p) { return (long long)p.x * p.x + (long long)p.z * p.z; }
static int bydist(const void *a, const void *b)
{
    long long da = dist2(*(const Pos *)a), db = dist2(*(const Pos *)b);
    return (da > db) - (da < db);
}

// Every viable structure of one type within `radius` blocks of (0,0), nearest first; returns the count.
static int nearby(int st, int mc, uint64_t seed, Generator *g, int radius, Pos *out, int max)
{
    StructureConfig sc;
    if (!getStructureConfig(st, mc, &sc)) return 0;
    int rb = sc.regionSize * 16, n = 0;
    int r0 = -radius / rb - 1, r1 = radius / rb;
    for (int rz = r0; rz <= r1; rz++)
        for (int rx = r0; rx <= r1; rx++) {
            Pos p;
            if (n >= max || !getStructurePos(st, mc, seed, rx, rz, &p)) continue;
            if (dist2(p) > (long long)radius * radius) continue;
            if (!isViableStructurePos(st, g, p.x, p.z, 0)) continue;
            out[n++] = p;
        }
    qsort(out, n, sizeof *out, bydist);
    return n;
}

static void printNearby(const char *name, const int *types, size_t count, int mc, uint64_t seed, Generator *g, int radius)
{
    printf("  \"%s\": {", name);
    int first = 1;
    for (size_t i = 0; i < count; i++) {
        Pos ps[64];
        int n = nearby(types[i], mc, seed, g, radius, ps, 64);
        if (!n) continue;
        printf("%s\n    \"%s\": [", first ? "" : ",", struct2str(types[i]));
        for (int k = 0; k < n && k < 6; k++) printf("%s[%d, %d]", k ? ", " : "", ps[k].x, ps[k].z);
        printf("]");
        first = 0;
    }
    printf("\n  },\n");
}

static int probe(int mc, uint64_t seed)
{
    Generator g;
    setupGenerator(&g, mc, 0);
    applySeed(&g, DIM_OVERWORLD, seed);

    uint64_t rng;
    Pos est = estimateSpawn(&g, &rng);
    Pos sp = getSpawn(&g);
    printf("{\n  \"version\": \"%s\", \"seed\": %lld,\n", mc2str(mc), (long long)seed);
    printf("  \"spawn\": {\"x\": %d, \"z\": %d, \"biome\": \"%s\", \"estimate\": [%d, %d]},\n",
        sp.x, sp.z, biome2str(mc, getBiomeAt(&g, 4, sp.x >> 2, 64 >> 2, sp.z >> 2)), est.x, est.z);

    printNearby("overworld", OVERWORLD_TYPES, sizeof OVERWORLD_TYPES / sizeof *OVERWORLD_TYPES, mc, seed, &g, 2000);
    printf("  \"strongholds\": [");
    StrongholdIter sh;
    initFirstStronghold(&sh, mc, seed);
    for (int i = 0; i < 3; i++) {
        nextStronghold(&sh, &g);
        printf("%s[%d, %d]", i ? ", " : "", sh.pos.x, sh.pos.z);
    }
    printf("],\n");

    Generator gn;
    setupGenerator(&gn, mc, 0);
    applySeed(&gn, DIM_NETHER, seed);
    printNearby("nether", NETHER_TYPES, sizeof NETHER_TYPES / sizeof *NETHER_TYPES, mc, seed, &gn, 1000);

    // End centre: surface height of the main island at a few points (0 = no terrain).
    static const int pts[][2] = { {0, 0}, {40, 0}, {0, 40}, {-40, 0}, {0, -40}, {80, 80} };
    int solid = 0;
    printf("  \"end\": {\"heights\": [");
    for (int i = 0; i < 6; i++) {
        int h = getEndSurfaceHeight(mc, seed, pts[i][0], pts[i][1]);
        if (h > 0) solid++;
        printf("%s%d", i ? ", " : "", h);
    }
    printf("], \"mainIsland\": %s}\n}\n", solid ? "true" : "false");
    return 0;
}

static int bench(int mc, double seconds)
{
    // Stage 1: village attempt position in the spawn region, for consecutive structure seeds.
    uint64_t s = 0x1234567ULL, hits = 0, n = 0;
    double t0 = now();
    while (now() - t0 < seconds) {
        for (int k = 0; k < 100000; k++, s++) {
            Pos p;
            if (getStructurePos(Village, mc, s, 0, 0, &p) && p.x < 200 && p.z < 200) hits++;
        }
        n += 100000;
    }
    double r1 = n / (now() - t0);

    // Stage 2: full biome viability for those close villages.
    Generator g;
    setupGenerator(&g, mc, 0);
    uint64_t m = 0, viable = 0;
    s = 0x1234567ULL;
    t0 = now();
    while (now() - t0 < seconds) {
        Pos p;
        s++;
        if (!getStructurePos(Village, mc, s, 0, 0, &p) || p.x >= 200 || p.z >= 200) continue;
        applySeed(&g, DIM_OVERWORLD, s);
        if (isViableStructurePos(Village, &g, p.x, p.z, 0)) viable++;
        m++;
    }
    double r2 = m / (now() - t0);
    printf("{\"version\": \"%s\", \"threads\": 1, \"stage1PerSec\": %.0f, \"stage1HitRate\": %.4f, "
        "\"stage2PerSec\": %.0f, \"stage2ViableRate\": %.4f}\n",
        mc2str(mc), r1, (double)hits / n, r2, m ? (double)viable / m : 0.0);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 3 && !strcmp(argv[1], "bench")) {
        int mc = str2mc(argv[2]);
        if (mc < 0) { fprintf(stderr, "unknown version %s\n", argv[2]); return 2; }
        return bench(mc, argc > 3 ? atof(argv[3]) : 2.0);
    }
    if (argc < 3) {
        fprintf(stderr, "usage: probe <version> <seed> | probe bench <version> [seconds]\n");
        return 2;
    }
    int mc = str2mc(argv[1]);
    if (mc < 0) { fprintf(stderr, "unknown version %s\n", argv[1]); return 2; }
    return probe(mc, (uint64_t)strtoll(argv[2], NULL, 10));
}
