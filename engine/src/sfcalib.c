#include "sfcalib.h"
#include "util.h"
#include "loot/cjson/cJSON.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#endif

#define STAT_THRESHOLD 0.01   // a stat only scores when it is in the rarest 1% (per tail)

static int cmpd(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

void calibBuild(Calib *cal, int mc, double **vals, long long n, double seconds)
{
    memset(cal, 0, sizeof *cal);
    cal->present = 1;
    snprintf(cal->version, sizeof cal->version, "%s", mc2str(mc));
    cal->samples = n;
    cal->seconds = seconds;
    for (int d = 0; d < DETECTOR_COUNT; d++) {
        CalDet *cd = &cal->det[d];
        if (!vals[d] || mc < DETECTORS[d].minMc) continue;
        cd->present = 1;
        cd->samples = n;
        if (DETECTORS[d].kind == KIND_BOOL) {
            for (long long i = 0; i < n; i++) cd->hits += vals[d][i] > 0.5;
            continue;
        }
        qsort(vals[d], n, sizeof(double), cmpd);
        for (int k = 0; k < CAL_Q; k++) cd->q[k] = vals[d][(long long)((double)k / (CAL_Q - 1) * (n - 1))];
        cd->nlo = cd->nhi = (int)(n < CAL_TAIL ? n : CAL_TAIL);
        for (int k = 0; k < cd->nlo; k++) cd->lo[k] = vals[d][k];
        for (int k = 0; k < cd->nhi; k++) cd->hi[k] = vals[d][n - 1 - k];
    }
}

static void putArr(FILE *f, const char *name, const double *a, int n)
{
    fprintf(f, "\"%s\": [", name);
    for (int i = 0; i < n; i++) fprintf(f, "%s%.6g", i ? "," : "", a[i]);
    fprintf(f, "]");
}

static void mkdirs(const char *path)
{
    char buf[512];
    snprintf(buf, sizeof buf, "%s", path);
    for (char *p = buf + 3; *p; p++)
        if (*p == '\\' || *p == '/') {
            char c = *p; *p = 0;
#ifdef _WIN32
            _mkdir(buf);
#endif
            *p = c;
        }
}

int calibSave(const Calib *cal, const char *path)
{
    mkdirs(path);
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    fprintf(f, "{\"version\": \"%s\", \"samples\": %lld, \"seconds\": %.1f, \"detectors\": {", cal->version, cal->samples, cal->seconds);
    int first = 1;
    for (int d = 0; d < DETECTOR_COUNT; d++) {
        const CalDet *cd = &cal->det[d];
        if (!cd->present) continue;
        fprintf(f, "%s\n\"%s\": {\"samples\": %lld", first ? "" : ",", DETECTORS[d].id, cd->samples);
        first = 0;
        if (DETECTORS[d].kind == KIND_BOOL) fprintf(f, ", \"hits\": %lld}", cd->hits);
        else {
            fprintf(f, ", ");
            putArr(f, "q", cd->q, CAL_Q); fprintf(f, ", ");
            putArr(f, "lo", cd->lo, cd->nlo); fprintf(f, ", ");
            putArr(f, "hi", cd->hi, cd->nhi); fprintf(f, "}");
        }
    }
    fprintf(f, "\n}}\n");
    fclose(f);
    return 1;
}

static int getArr(cJSON *o, const char *name, double *out, int max)
{
    cJSON *a = cJSON_GetObjectItem(o, name);
    int n = 0;
    cJSON *it;
    cJSON_ArrayForEach(it, a) { if (n < max) out[n++] = it->valuedouble; }
    return n;
}

int calibLoad(Calib *cal, const char *path)
{
    memset(cal, 0, sizeof *cal);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(len + 1);
    if (fread(buf, 1, len, f) != (size_t)len) { fclose(f); free(buf); return 0; }
    buf[len] = 0;
    fclose(f);
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return 0;
    cJSON *v = cJSON_GetObjectItem(root, "version");
    if (cJSON_IsString(v)) snprintf(cal->version, sizeof cal->version, "%s", v->valuestring);
    cal->samples = (long long)cJSON_GetObjectItem(root, "samples")->valuedouble;
    cJSON *dets = cJSON_GetObjectItem(root, "detectors"), *it;
    cJSON_ArrayForEach(it, dets) {
        int d = detectorIndex(it->string);
        if (d < 0) continue;
        CalDet *cd = &cal->det[d];
        cd->present = 1;
        cd->samples = (long long)cJSON_GetObjectItem(it, "samples")->valuedouble;
        if (DETECTORS[d].kind == KIND_BOOL) cd->hits = (long long)cJSON_GetObjectItem(it, "hits")->valuedouble;
        else {
            if (getArr(it, "q", cd->q, CAL_Q) != CAL_Q) cd->present = 0;
            cd->nlo = getArr(it, "lo", cd->lo, CAL_TAIL);
            cd->nhi = getArr(it, "hi", cd->hi, CAL_TAIL);
        }
    }
    cJSON_Delete(root);
    cal->present = 1;
    return 1;
}

// Fraction of calibration samples at least as extreme as v in one tail.
static double tailP(const CalDet *cd, double v, int high, int *beyond)
{
    double n = (double)cd->samples;
    const double *t = high ? cd->hi : cd->lo;
    int nt = high ? cd->nhi : cd->nlo;
    *beyond = 0;
    // Exact tails first.
    int cnt = 0;
    for (int k = 0; k < nt; k++) {
        if (high ? t[k] >= v : t[k] <= v) cnt++;
        else break;
    }
    if (cnt < nt) {
        if (cnt == 0) { *beyond = 1; return 0.5 / n; }  // more extreme than every sample
        return cnt / n;
    }
    // Inside the body: use quantiles.
    if (high) {
        int k = CAL_Q - 1;
        while (k > 0 && cd->q[k - 1] >= v) k--;
        return (double)(CAL_Q - k) / (CAL_Q - 1);
    } else {
        int k = 0;
        while (k < CAL_Q - 1 && cd->q[k + 1] <= v) k++;
        return (double)(k + 1) / (CAL_Q - 1);
    }
}

double calibScore(const Calib *cal, int d, double value, int *rarer)
{
    const Detector *det = &DETECTORS[d];
    const CalDet *cd = cal && cal->present ? &cal->det[d] : NULL;
    *rarer = 0;
    if (det->kind == KIND_BOOL) {
        if (value < 0.5) return 0;
        double rate;
        if (cd && cd->present && cd->hits > 0) rate = (double)cd->hits / cd->samples;
        else if (cd && cd->present) { rate = 1.0 / (cd->samples + 1); if (rate > det->prior) rate = det->prior; *rarer = 1; }
        else rate = det->prior;
        return -log10(rate);
    }
    if (!cd || !cd->present) return 0;
    double best = 0;
    for (int high = 0; high <= 1; high++) {
        if (!(det->tail & (high ? TAIL_HIGH : TAIL_LOW))) continue;
        int beyond;
        double p = tailP(cd, value, high, &beyond);
        if (p < STAT_THRESHOLD && -log10(p) > best) { best = -log10(p); *rarer = beyond; }
    }
    return best;
}

void calibDefaultPath(char *out, size_t size, const char *version)
{
    const char *base = getenv("LOCALAPPDATA");
    snprintf(out, size, "%s\\SeedFindr\\calib\\%s.json", base ? base : ".", version);
}
