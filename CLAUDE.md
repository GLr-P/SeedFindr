# CLAUDE.md

SeedFindr: a free Windows app that hunts random Minecraft **Java** seeds (1.16.1 and 1.17 to 26.x, never Bedrock) for rare, broken,
funny and speedrun-worthy ones. The owner is not a programmer: make technical decisions yourself, verify by running things, never ask them to edit code or run commands.

This folder is the public code repo. The private project files sit one level up: `../ABOUTME.md` (status, decisions),
`../Information/PLAN.md` (phases, funnel, detectors, speed plan, verification log), `../Research/seed-finding.md` (fork API quick reference), `../Handoff/`.

## Layout

| Path | What |
|---|---|
| `engine/cubiomes/` | git submodule: the **xpple/cubiomes** fork (MIT). Never edit it here; pin a commit and report bugs upstream |
| `engine/src/` | SeedFindr's own C code. A file with `main()` becomes `engine/build/<name>.exe`; other `.c` files are linked into every program |
| `engine/build.mjs` | builds with Zig's Clang (`zig cc`), caching objects in `engine/build/obj/` |
| `app/` | Electron + React + TypeScript app (Phase 5, not started) |

## Commands

```sh
git submodule update --init          # after cloning
node engine/build.mjs [program]      # build (first build ~100 s, then only changed files)
engine/build/probe.exe 1.16.1 12345  # what the engine sees for one seed (JSON)
engine/build/probe.exe bench 26.3 3  # single-thread seeds/s for stage 1 and 2
engine/build/biomecol.exe 26.3 <seed> <x> <z>              # biome by height + solid ground top at one column
node engine/tests/spotcheck.mjs 26.3 12345 8675309         # engine vs the official server (~30-60 s per seed)
node app/checker/realcheck.mjs 1.16.1 <seed> [label:x:z]   # ask the server directly (spawn, /locate)
```

The real-game checker caches server jars and portable Java in `%LOCALAPPDATA%\SeedFindr\cache`; the owner accepted the Minecraft EULA on 2026-11-20 (recorded there).

## Rules

- Compile flags: `-O3 -fwrapv` always (Java integer overflow semantics). The fork doesn't support MSVC.
- `zig cc` targets the build machine's CPU by default; release builds must pick portable targets (PLAN.md Phase 4).
- Versions are strings like `1.16.1`, `26.3` (`str2mc`). Use `1.16.1` for speedrun seeds, not 1.16.5.
- Zig is installed by winget; `build.mjs` finds it even when it's not on PATH. Node may also be missing from PATH: `export PATH="/c/Program Files/nodejs:$PATH"`.
- Speed rules (PLAN.md Phase 4): cheapest check first, stop on first failure, structure seed (48-bit) before world seed, no allocation in hot loops.
- Every detector needs a test seed confirmed in the real game (or a Mojang bug report).
- Structure checks go through `sfViableStructure` (`engine/src/sfterrain.c`), not the fork's `isViableStructurePos` alone: in 1.18+ desert pyramids and jungle temples also need solid ground at y >= 63 on all four footprint corners, which the fork only approximates (and got wrong for 3 of the first 6 jungle temples tested).
- `/locate` in 1.16 returns the first hit in ring search order, not the nearest; verify a prediction by locating from its own position (spotcheck.mjs does).
