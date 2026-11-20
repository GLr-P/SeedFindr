// hunt: the SeedFindr search engine. Hunts random seeds on every CPU thread and prints JSON lines.
//
//   hunt --version 1.16.1 [options]
//     --threads N         worker threads (default: all but 2)
//     --seconds S         stop after S seconds          --count N   stop after N seeds
//     --sisters K         world seeds tried per structure seed (1 = plain random; 65536 = all sisters)
//     --require EXPR      gate, repeatable: "village_dist<=200", "spawn_height>=120", "end_missing_island"
//     --only a,b,c        score only these detectors (default: all for the version)
//     --min-score X       report seeds scoring at least X (= rarer than 1 in 10^X). Default 3, or 0 with --require
//     --rng N             base of the seed sequence (same N = same seeds, any thread count)
//     --calibrate N       measure every detector on N random seeds and save the calibration
//     --calib PATH        calibration file (default %LOCALAPPDATA%\SeedFindr\calib\<version>.json)
//     --priority P        normal | low (default) | idle
//     --progress-ms MS    progress line interval (default 500; 0 = none)
//     --test a,b,c        score these seeds (exact spawn, no gates) and print one hit line each, then exit
//     --list              print the detectors as JSON and exit
//   While running, a line "stop" on stdin ends the hunt cleanly (as does Ctrl+C).
//
// Output (one JSON object per line): {"type":"start"...}, {"type":"progress"...}, {"type":"hit"...}, {"type":"done"...}
#include "sfcalib.h"
#include "sfdetect.h"
#include "util.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define BATCH 64
#define MAX_GATES 32

typedef struct { int d; int op; double v; } Gate; // op: 0 = true, 1 = <=, 2 = >=, 3 = <, 4 = >

static struct {
    int mc;
    int threads;
    double seconds;
    long long count;
    int sisters;
    Gate gates[MAX_GATES];
    int ngates;
    int enabled[64];
    double minScore;
    uint64_t rng;
    long long calibrate;
    char calibPath[512];
    int progressMs;
} cfg;

static Calib cal;
static volatile LONG stopFlag = 0;
static volatile LONG64 nextBatch = 0;
static volatile LONG64 worldDone = 0, structDone = 0, hitsFound = 0;
static CRITICAL_SECTION outLock;
static double **calVals; // calibration mode: [detector][seed index]

static uint64_t mix64(uint64_t z)
{
    z += 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static double nowSec(void)
{
    static LARGE_INTEGER f;
    LARGE_INTEGER t;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / f.QuadPart;
}

// ---- per-thread detector cache ----
typedef struct {
    SeedCtx c;
    DetOut out[64];
    uint64_t key[64]; // seed (or s48+1 for structure-stage detectors) the cached result belongs to; 0 = none
} Worker;

static DetOut *runDet(Worker *w, int d)
{
    const Detector *det = &DETECTORS[d];
    uint64_t key = det->stage == STAGE_STRUCTURE ? (w->c.s48 | (1ULL << 63)) : w->c.seed ^ 0x5bd1e995ULL;
    key ^= (uint64_t)w->c.exactSpawn << 62;
    if (w->key[d] != key || !w->key[d]) {
        memset(&w->out[d], 0, sizeof w->out[d]);
        det->run(&w->c, &w->out[d]);
        w->key[d] = key ? key : 1;
    }
    return &w->out[d];
}

static int gatesPass(Worker *w, int stage)
{
    for (int i = 0; i < cfg.ngates; i++) {
        Gate *g = &cfg.gates[i];
        if (DETECTORS[g->d].stage != stage) continue;
        double v = runDet(w, g->d)->value;
        int ok = g->op == 0 ? v > 0.5 : g->op == 1 ? v <= g->v : g->op == 2 ? v >= g->v : g->op == 3 ? v < g->v : v > g->v;
        if (!ok) return 0;
    }
    return 1;
}

static int allGatesPass(Worker *w)
{
    return gatesPass(w, STAGE_STRUCTURE) && gatesPass(w, STAGE_BIOME) && gatesPass(w, STAGE_TERRAIN);
}

// Score every enabled detector; returns total and fills per-detector scores.
static double scoreSeed(Worker *w, double *scores, int *rarer)
{
    double total = 0;
    for (int d = 0; d < DETECTOR_COUNT; d++) {
        scores[d] = 0; rarer[d] = 0;
        if (!cfg.enabled[d]) continue;
        DetOut *o = runDet(w, d);
        scores[d] = calibScore(&cal, d, o->value, &rarer[d]);
        total += scores[d];
    }
    return total;
}

static void report(Worker *w, uint64_t seed)
{
    // Re-check with the exact spawn point before reporting.
    w->c.exactSpawn = 1;
    ctxSetSeed(&w->c, seed);
    double scores[64];
    int rarer[64];
    int ok = allGatesPass(w);
    double total = ok ? scoreSeed(w, scores, rarer) : 0;
    w->c.exactSpawn = 0;
    if (!ok || total < cfg.minScore) return;
    InterlockedIncrement64(&hitsFound);

    char line[8192];
    int n = 0;
    Pos sp = w->c.spawn;
    n += snprintf(line + n, sizeof line - n, "{\"type\":\"hit\",\"version\":\"%s\",\"seed\":\"%lld\",\"score\":%.2f,\"odds\":%.4g,"
        "\"spawn\":[%d,%d],\"tags\":[", mc2str(cfg.mc), (long long)seed, total, pow(10, total), sp.x, sp.z);
    int first = 1;
    for (int d = 0; d < DETECTOR_COUNT; d++)
        if (cfg.enabled[d] && scores[d] > 0) { n += snprintf(line + n, sizeof line - n, "%s\"%s\"", first ? "" : ",", DETECTORS[d].id); first = 0; }
    n += snprintf(line + n, sizeof line - n, "],\"details\":[");
    first = 1;
    for (int d = 0; d < DETECTOR_COUNT; d++) {
        if (!cfg.enabled[d] && !(cfg.ngates && w->key[d])) continue;
        DetOut *o = &w->out[d];
        n += snprintf(line + n, sizeof line - n, "%s{\"id\":\"%s\",\"value\":%.6g,\"score\":%.2f%s%s", first ? "" : ",",
            DETECTORS[d].id, o->value, cfg.enabled[d] ? scores[d] : 0, rarer[d] ? ",\"rarerThanCalibration\":true" : "",
            DETECTORS[d].confirm && scores[d] > 0 ? ",\"confirmInGame\":true" : "");
        if (o->hasPos) n += snprintf(line + n, sizeof line - n, ",\"pos\":[%d,%d]", o->x, o->z);
        n += snprintf(line + n, sizeof line - n, ",\"desc\":\"%s\"}", o->desc);
        first = 0;
    }
    n += snprintf(line + n, sizeof line - n, "]}\n");
    EnterCriticalSection(&outLock);
    fputs(line, stdout);
    fflush(stdout);
    LeaveCriticalSection(&outLock);
}

static DWORD WINAPI worker(LPVOID arg)
{
    (void)arg;
    Worker *w = calloc(1, sizeof *w);
    ctxInit(&w->c, cfg.mc);
    const int K = cfg.calibrate ? 1 : cfg.sisters;
    while (!stopFlag) {
        LONG64 b = InterlockedIncrement64(&nextBatch) - 1;
        long long first = b * BATCH;
        long long limit = cfg.calibrate ? cfg.calibrate : 0;
        if (limit && first >= limit) break;
        for (int j = 0; j < BATCH && !stopFlag; j++) {
            long long idx = first + j;
            if (limit && idx >= limit) break;
            uint64_t h = mix64(cfg.rng * 0x9E3779B97F4A7C15ULL + (uint64_t)idx);
            ctxSetSeed(&w->c, h);
            if (cfg.calibrate) {
                for (int d = 0; d < DETECTOR_COUNT; d++)
                    if (calVals[d]) calVals[d][idx] = runDet(w, d)->value;
                InterlockedIncrement64(&worldDone);
                continue;
            }
            InterlockedIncrement64(&structDone);
            if (!gatesPass(w, STAGE_STRUCTURE)) { InterlockedAdd64(&worldDone, K); continue; }
            for (int k = 0; k < K && !stopFlag; k++) {
                uint64_t seed = K == 1 ? h : (((((h >> 48) + (uint64_t)k) & 0xFFFF)) << 48) | (h & MASK48);
                ctxSetSeed(&w->c, seed);
                if (gatesPass(w, STAGE_BIOME) && gatesPass(w, STAGE_TERRAIN)) {
                    double scores[64];
                    int rarer[64];
                    if (scoreSeed(w, scores, rarer) >= cfg.minScore) report(w, seed);
                }
            }
            InterlockedAdd64(&worldDone, K);
            if (cfg.count && worldDone >= cfg.count) stopFlag = 1;
        }
    }
    free(w);
    return 0;
}

static DWORD WINAPI stdinWatcher(LPVOID arg)
{
    (void)arg;
    char buf[256];
    while (fgets(buf, sizeof buf, stdin))
        if (!strncmp(buf, "stop", 4)) { stopFlag = 1; break; }
    return 0;
}

static BOOL WINAPI ctrlHandler(DWORD t) { (void)t; stopFlag = 1; return TRUE; }

static int parseGate(const char *s, Gate *g)
{
    char id[64];
    const char *ops[] = { "<=", ">=", "<", ">" };
    for (int i = 0; i < 4; i++) {
        const char *p = strstr(s, ops[i]);
        if (!p) continue;
        size_t len = p - s;
        if (len >= sizeof id) return 0;
        memcpy(id, s, len); id[len] = 0;
        g->d = detectorIndex(id);
        g->op = i + 1;
        g->v = atof(p + strlen(ops[i]));
        return g->d >= 0;
    }
    g->d = detectorIndex(s);
    g->op = 0;
    return g->d >= 0 && DETECTORS[g->d].kind == KIND_BOOL;
}

static void listDetectors(void)
{
    static const char *stages[] = { "", "structure", "biome", "terrain" };
    static const char *tails[] = { "", "low", "high", "both" };
    printf("[");
    for (int d = 0; d < DETECTOR_COUNT; d++)
        printf("%s\n{\"id\":\"%s\",\"title\":\"%s\",\"stage\":\"%s\",\"kind\":\"%s\",\"tail\":\"%s\",\"since\":\"%s\",\"confirmInGame\":%s}", d ? "," : "",
            DETECTORS[d].id, DETECTORS[d].title, stages[DETECTORS[d].stage], DETECTORS[d].kind == KIND_BOOL ? "bool" : "stat",
            tails[DETECTORS[d].tail], mc2str(DETECTORS[d].minMc), DETECTORS[d].confirm ? "true" : "false");
    printf("\n]\n");
}

static void usage(void)
{
    fprintf(stderr, "usage: hunt --version <v> [--threads N] [--seconds S] [--count N] [--sisters K] [--require EXPR]...\n"
        "            [--only a,b] [--min-score X] [--rng N] [--calibrate N] [--calib PATH] [--priority P] [--list]\n");
    exit(2);
}

int main(int argc, char **argv)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    cfg.threads = si.dwNumberOfProcessors > 3 ? (int)si.dwNumberOfProcessors - 2 : 1;
    cfg.sisters = 1;
    cfg.minScore = -1;
    cfg.progressMs = 500;
    cfg.rng = (uint64_t)GetTickCount64() ^ ((uint64_t)GetCurrentProcessId() << 32);
    const char *priority = "low", *only = NULL, *test = NULL;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--list")) { listDetectors(); return 0; }
#define NEED() do { if (!v) usage(); i++; } while (0)
        if (!strcmp(a, "--version")) { NEED(); cfg.mc = str2mc(v); }
        else if (!strcmp(a, "--threads")) { NEED(); cfg.threads = atoi(v); }
        else if (!strcmp(a, "--seconds")) { NEED(); cfg.seconds = atof(v); }
        else if (!strcmp(a, "--count")) { NEED(); cfg.count = atoll(v); }
        else if (!strcmp(a, "--sisters")) { NEED(); cfg.sisters = atoi(v); }
        else if (!strcmp(a, "--require")) {
            NEED();
            if (cfg.ngates >= MAX_GATES || !parseGate(v, &cfg.gates[cfg.ngates])) { fprintf(stderr, "bad --require %s\n", v); return 2; }
            cfg.ngates++;
        }
        else if (!strcmp(a, "--only")) { NEED(); only = v; }
        else if (!strcmp(a, "--test")) { NEED(); test = v; }
        else if (!strcmp(a, "--min-score")) { NEED(); cfg.minScore = atof(v); }
        else if (!strcmp(a, "--rng")) { NEED(); cfg.rng = strtoull(v, NULL, 10); }
        else if (!strcmp(a, "--calibrate")) { NEED(); cfg.calibrate = atoll(v); }
        else if (!strcmp(a, "--calib")) { NEED(); snprintf(cfg.calibPath, sizeof cfg.calibPath, "%s", v); }
        else if (!strcmp(a, "--priority")) { NEED(); priority = v; }
        else if (!strcmp(a, "--progress-ms")) { NEED(); cfg.progressMs = atoi(v); }
        else usage();
    }
    if (cfg.mc <= 0) { fprintf(stderr, "--version is required (e.g. 1.16.1, 26.3)\n"); return 2; }
    if (cfg.threads < 1) cfg.threads = 1;
    if (cfg.sisters < 1) cfg.sisters = 1;
    if (cfg.sisters > 65536) cfg.sisters = 65536;
    if (cfg.minScore < 0) cfg.minScore = cfg.ngates ? 0 : 3;
    for (int d = 0; d < DETECTOR_COUNT; d++) cfg.enabled[d] = cfg.mc >= DETECTORS[d].minMc;
    if (only) {
        for (int d = 0; d < DETECTOR_COUNT; d++) cfg.enabled[d] = 0;
        char buf[1024];
        snprintf(buf, sizeof buf, "%s", only);
        for (char *t = strtok(buf, ","); t; t = strtok(NULL, ",")) {
            int d = detectorIndex(t);
            if (d < 0) { fprintf(stderr, "unknown detector %s\n", t); return 2; }
            cfg.enabled[d] = cfg.mc >= DETECTORS[d].minMc;
        }
    }
    if (!cfg.calibPath[0]) calibDefaultPath(cfg.calibPath, sizeof cfg.calibPath, mc2str(cfg.mc));
    if (!cfg.calibrate) calibLoad(&cal, cfg.calibPath);

    if (test) {
        InitializeCriticalSection(&outLock);
        cfg.ngates = 0;
        cfg.minScore = -1e9;
        Worker *w = calloc(1, sizeof *w);
        ctxInit(&w->c, cfg.mc);
        char buf[4096];
        snprintf(buf, sizeof buf, "%s", test);
        for (char *s = strtok(buf, ","); s; s = strtok(NULL, ",")) report(w, (uint64_t)strtoll(s, NULL, 10));
        return 0;
    }

    SetPriorityClass(GetCurrentProcess(), !strcmp(priority, "idle") ? IDLE_PRIORITY_CLASS
        : !strcmp(priority, "normal") ? NORMAL_PRIORITY_CLASS : BELOW_NORMAL_PRIORITY_CLASS);
    SetConsoleCtrlHandler(ctrlHandler, TRUE);
    InitializeCriticalSection(&outLock);

    if (cfg.calibrate) {
        calVals = calloc(DETECTOR_COUNT, sizeof *calVals);
        for (int d = 0; d < DETECTOR_COUNT; d++)
            if (cfg.mc >= DETECTORS[d].minMc) calVals[d] = malloc(cfg.calibrate * sizeof(double));
    }

    printf("{\"type\":\"start\",\"version\":\"%s\",\"threads\":%d,\"sisters\":%d,\"gates\":%d,\"minScore\":%.2f,\"rng\":%llu,"
        "\"calibration\":%s,\"mode\":\"%s\"}\n", mc2str(cfg.mc), cfg.threads, cfg.sisters, cfg.ngates, cfg.minScore,
        (unsigned long long)cfg.rng, cal.present ? "true" : "false", cfg.calibrate ? "calibrate" : "hunt");
    fflush(stdout);

    CreateThread(NULL, 0, stdinWatcher, NULL, 0, NULL);
    HANDLE *th = calloc(cfg.threads, sizeof *th);
    double t0 = nowSec();
    for (int i = 0; i < cfg.threads; i++) th[i] = CreateThread(NULL, 0, worker, NULL, 0, NULL);

    double lastP = t0;
    long long lastW = 0;
    for (;;) {
        DWORD r = WaitForMultipleObjects(cfg.threads > 64 ? 64 : cfg.threads, th, TRUE, 50);
        double t = nowSec();
        if (cfg.seconds > 0 && t - t0 >= cfg.seconds) stopFlag = 1;
        if (r != WAIT_TIMEOUT) break;
        if (cfg.progressMs > 0 && (t - lastP) * 1000 >= cfg.progressMs) {
            long long wd = worldDone;
            EnterCriticalSection(&outLock);
            printf("{\"type\":\"progress\",\"elapsed\":%.1f,\"seeds\":%lld,\"structureSeeds\":%lld,\"seedsPerSec\":%.0f,\"avgPerSec\":%.0f,\"hits\":%lld}\n",
                t - t0, wd, (long long)structDone, (wd - lastW) / (t - lastP), wd / (t - t0), (long long)hitsFound);
            fflush(stdout);
            LeaveCriticalSection(&outLock);
            lastP = t; lastW = wd;
        }
    }
    double el = nowSec() - t0;

    if (cfg.calibrate) {
        long long n = worldDone < cfg.calibrate ? worldDone : cfg.calibrate;
        Calib out;
        calibBuild(&out, cfg.mc, calVals, n, el);
        int saved = n == cfg.calibrate && calibSave(&out, cfg.calibPath);
        printf("{\"type\":\"calibration\",\"version\":\"%s\",\"samples\":%lld,\"seconds\":%.1f,\"saved\":%s,\"path\":\"", mc2str(cfg.mc), n, el, saved ? "true" : "false");
        for (const char *p = cfg.calibPath; *p; p++) { if (*p == '\\') putchar('\\'); putchar(*p); }
        printf("\",\"detectors\":{");
        int first = 1;
        for (int d = 0; d < DETECTOR_COUNT; d++) {
            if (!out.det[d].present) continue;
            if (DETECTORS[d].kind == KIND_BOOL) printf("%s\"%s\":{\"hits\":%lld}", first ? "" : ",", DETECTORS[d].id, out.det[d].hits);
            else printf("%s\"%s\":{\"min\":%g,\"p1\":%g,\"median\":%g,\"p99\":%g,\"max\":%g}", first ? "" : ",", DETECTORS[d].id,
                out.det[d].lo[0], out.det[d].q[10], out.det[d].q[500], out.det[d].q[990], out.det[d].hi[0]);
            first = 0;
        }
        printf("}}\n");
    }
    printf("{\"type\":\"done\",\"elapsed\":%.2f,\"seeds\":%lld,\"structureSeeds\":%lld,\"seedsPerSec\":%.0f,\"hits\":%lld,\"stopped\":%s}\n",
        el, (long long)worldDone, (long long)structDone, worldDone / el, (long long)hitsFound, stopFlag ? "true" : "false");
    fflush(stdout);
    return 0;
}
