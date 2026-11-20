// Detectors: small functions that measure one thing about a seed.
// A BOOL detector says yes/no (a rare event); a STAT detector returns a number whose unusual values matter
// (the anomaly hunter scores how far into the calibrated tail a seed lands).
#ifndef SFDETECT_H
#define SFDETECT_H

#include "sfctx.h"

enum { STAGE_STRUCTURE = 1, STAGE_BIOME = 2, STAGE_TERRAIN = 3 };  // cost order; STRUCTURE = 48-bit only
enum { KIND_BOOL, KIND_STAT };
enum { TAIL_LOW = 1, TAIL_HIGH = 2, TAIL_BOTH = 3 };

typedef struct {
    double value;      // STAT: the measurement; BOOL: 1 = hit, 0 = no hit
    int hasPos, x, z;  // where (if it has a place)
    char desc[120];    // one line for people, e.g. "Village 96 blocks from spawn"
} DetOut;

typedef struct {
    const char *id;
    const char *title;
    int stage, kind, tail;
    int minMc;           // first version it applies to
    double prior;        // BOOL: rough hit rate before calibration
    int confirm;         // 1 = engine result is approximate: a find must be confirmed in the real game (stage 4)
    void (*run)(SeedCtx *c, DetOut *o);
} Detector;

extern const Detector DETECTORS[];
extern const int DETECTOR_COUNT;

int detectorIndex(const char *id);   // -1 if unknown

#endif
