#include "sfdetect.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// ---- Stage 1: structure seed (lower 48 bits); results are shared by all 65,536 sister seeds ----

// MC-267423: the main End island does not generate (only pillars and bedrock). Seed list in the bug report.
static void endMissingIsland(SeedCtx *c, DetOut *o)
{
    int solid = ctxEndHeight(c, 0) > 0;   // a normal island is solid at the centre: one sample decides most seeds
    for (int i = 1; i < 6 && !solid; i++) solid = ctxEndHeight(c, i) > 0;
    o->value = !solid;
    o->hasPos = 1; o->x = 0; o->z = 0;
    snprintf(o->desc, sizeof o->desc, "No main End island (Mojang bug MC-267423): the End can't be finished normally");
}

// MC-93185 family: the island has a hole at 0,0, so the exit portal ends up at the bottom of the world.
static void endHoledIsland(SeedCtx *c, DetOut *o)
{
    int ring = 0;
    if (ctxEndHeight(c, 0) == 0)
        for (int i = 1; i <= 4; i++) ring += ctxEndHeight(c, i) > 0;
    o->value = ring >= 2;
    o->hasPos = 1; o->x = 0; o->z = 0;
    snprintf(o->desc, sizeof o->desc, "Hole in the middle of the End island: the exit portal falls to the bottom");
}

static void strongholdDist(SeedCtx *c, DetOut *o)
{
    Pos p = c->strongholdApprox;
    o->value = sqrt((double)sfDist2(p.x, p.z, 0, 0));
    o->hasPos = 1; o->x = p.x; o->z = p.z;
    snprintf(o->desc, sizeof o->desc, "First stronghold about %.0f blocks from 0,0", o->value);
}

// ---- Stage 2: biomes (full seed) ----

static void nearestFromSpawn(SeedCtx *c, DetOut *o, int st, int maxDist, const char *name)
{
    Pos s = ctxSpawn(c), p;
    int d = ctxNearest(c, st, s.x, s.z, maxDist, &p);
    o->value = d < 0 ? maxDist : d;
    if (d >= 0) { o->hasPos = 1; o->x = p.x; o->z = p.z; }
    if (d >= 0) snprintf(o->desc, sizeof o->desc, "%s %d blocks from spawn", name, d);
    else snprintf(o->desc, sizeof o->desc, "No %s within %d blocks of spawn", name, maxDist);
}

static void villageDist(SeedCtx *c, DetOut *o) { nearestFromSpawn(c, o, Village, 1500, "Village"); }
static void mansionDist(SeedCtx *c, DetOut *o) { nearestFromSpawn(c, o, Mansion, 4000, "Woodland mansion"); }

static void netherFromOrigin(SeedCtx *c, DetOut *o, int st, const char *name)
{
    Pos p;
    int d = ctxNearest(c, st, 0, 0, 1000, &p);
    o->value = d < 0 ? 1000 : d;
    if (d >= 0) { o->hasPos = 1; o->x = p.x; o->z = p.z; }
    if (d >= 0) snprintf(o->desc, sizeof o->desc, "%s %d blocks from 0,0 in the Nether", name, d);
    else snprintf(o->desc, sizeof o->desc, "No %s within 1000 blocks of 0,0 in the Nether", name);
}

static void bastionDist(SeedCtx *c, DetOut *o) { netherFromOrigin(c, o, Bastion, "Bastion"); }
static void fortressDist(SeedCtx *c, DetOut *o) { netherFromOrigin(c, o, Fortress, "Fortress"); }

static const int SPAWN_AREA_TYPES[] = { Village, Desert_Pyramid, Jungle_Temple, Swamp_Hut, Igloo, Shipwreck,
    Ocean_Ruin, Treasure, Ruined_Portal, Outpost, Monument, Mansion };

static void structuresNearSpawn(SeedCtx *c, DetOut *o)
{
    Pos s = ctxSpawn(c);
    int n = 0;
    for (size_t i = 0; i < sizeof SPAWN_AREA_TYPES / sizeof *SPAWN_AREA_TYPES; i++)
        n += ctxCountNear(c, SPAWN_AREA_TYPES[i], s.x, s.z, 256, 8);
    o->value = n;
    o->hasPos = 1; o->x = s.x; o->z = s.z;
    snprintf(o->desc, sizeof o->desc, "%d structures within 256 blocks of spawn", n);
}

// ---- Stage 3: terrain ----
// Checked against the real game (26.3, 2026-11-20): exact on land; trees are not counted. In 1.18+ the fork's terrain can
// show cave openings at or below sea level that the real game fills in (a spawn read as y=-58 was y=60), so low values
// must be confirmed in the real game.

static void spawnHeight(SeedCtx *c, DetOut *o)
{
    Pos s = ctxSpawn(c);
    int y = ctxSpawnY(c);
    o->value = y;
    o->hasPos = 1; o->x = s.x; o->z = s.z;
    snprintf(o->desc, sizeof o->desc, "Spawn ground at y=%d", y);
}

const Detector DETECTORS[] = {
    // id                      title                         stage            kind       tail       minMc     prior  confirm run
    { "end_missing_island",    "No main End island",         STAGE_STRUCTURE, KIND_BOOL, TAIL_HIGH, MC_1_9,   1e-7,  0, endMissingIsland },
    { "end_holed_island",      "Hole in the End island",     STAGE_STRUCTURE, KIND_BOOL, TAIL_HIGH, MC_1_9,   1e-6,  0, endHoledIsland },
    { "stronghold_dist",       "Stronghold distance",        STAGE_STRUCTURE, KIND_STAT, TAIL_LOW,  MC_1_9,   0,     0, strongholdDist },
    { "village_dist",          "Village distance",           STAGE_BIOME,     KIND_STAT, TAIL_LOW,  MC_1_0,   0,     0, villageDist },
    { "bastion_dist",          "Bastion distance",           STAGE_BIOME,     KIND_STAT, TAIL_LOW,  MC_1_16_1,0,     0, bastionDist },
    { "fortress_dist",         "Fortress distance",          STAGE_BIOME,     KIND_STAT, TAIL_LOW,  MC_1_0,   0,     0, fortressDist },
    { "structures_near_spawn", "Structures near spawn",      STAGE_BIOME,     KIND_STAT, TAIL_HIGH, MC_1_0,   0,     0, structuresNearSpawn },
    { "mansion_dist",          "Mansion distance",           STAGE_BIOME,     KIND_STAT, TAIL_LOW,  MC_1_11,  0,     0, mansionDist },
    { "spawn_height",          "Spawn height",               STAGE_TERRAIN,   KIND_STAT, TAIL_BOTH, MC_1_14,  0,     1, spawnHeight },
};
const int DETECTOR_COUNT = sizeof DETECTORS / sizeof *DETECTORS;

int detectorIndex(const char *id)
{
    for (int i = 0; i < DETECTOR_COUNT; i++)
        if (!strcmp(DETECTORS[i].id, id)) return i;
    return -1;
}
