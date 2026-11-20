// Calibration: how often each detector fires and how each stat is distributed on random seeds,
// so a find can be scored as -log10(chance) and shown as "about 1 in N seeds".
#ifndef SFCALIB_H
#define SFCALIB_H

#include "sfdetect.h"

#define CAL_Q 1001     // quantiles 0.000 .. 1.000
#define CAL_TAIL 200   // exact smallest/largest values kept for the tails

typedef struct {
    int present;
    long long samples;
    long long hits;               // BOOL
    double q[CAL_Q];              // STAT quantiles
    double lo[CAL_TAIL], hi[CAL_TAIL]; // STAT: smallest ascending, largest descending
    int nlo, nhi;
} CalDet;

typedef struct {
    int present;
    char version[16];
    long long samples;
    double seconds;
    CalDet det[32];
} Calib;

// Build a calibration from raw values: vals[d] holds n values per detector (BOOL: 0/1).
void calibBuild(Calib *cal, int mc, double **vals, long long n, double seconds);
int calibSave(const Calib *cal, const char *path);
int calibLoad(Calib *cal, const char *path);   // 1 = loaded

// Score of one detector result: 0 = nothing unusual; otherwise -log10(probability of a result this extreme).
// *rarer is set when the result is beyond everything seen in calibration (true odds are at least the score).
double calibScore(const Calib *cal, int d, double value, int *rarer);

// Default calibration path: %LOCALAPPDATA%\SeedFindr\calib\<version>.json
void calibDefaultPath(char *out, size_t size, const char *version);

#endif
