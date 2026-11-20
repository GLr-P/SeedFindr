# SeedFindr

A free, open source Windows app that hunts random Minecraft **Java Edition** seeds and finds the strange, rare, broken and
speedrun-worthy ones nobody has posted yet: structures merged into each other, worlds with no End island, absurd spawns,
great (or terrible) 1.16.1 speedrun seeds and MCSR-style practice seeds.

**Status: early development.** The search engine is being built; there is no app to download yet.

- Versions: 1.16.1 (speedrun) and 1.17 to 26.3.
- Engine: C, built on the [xpple/cubiomes](https://github.com/xpple/cubiomes) fork of [cubiomes](https://github.com/Cubitect/cubiomes) (MIT).
- Every find can be checked in the real game before you see it.

## Build (developers)

Needs [Zig](https://ziglang.org/) (as the C compiler) and Node.js.

```sh
git clone --recurse-submodules https://github.com/GLr-P/SeedFindr
cd SeedFindr
node engine/build.mjs
engine/build/probe.exe 1.16.1 12345
```

## Licence

MIT. Not affiliated with Mojang or Microsoft. Minecraft is a trademark of Mojang Synergies AB.
