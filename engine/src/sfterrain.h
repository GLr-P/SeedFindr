// SeedFindr terrain helpers on top of the fork's terrain noise.
#ifndef SFTERRAIN_H
#define SFTERRAIN_H

#include "finders.h"
#include "terrainnoise.h"

// Y of the highest solid noise block at a 1.18+ Overworld column (no water, no features), or -64 if none.
int sfSolidTop(TerrainNoise *tn, int x, int z);

// Full structure check: the fork's biome check plus exact terrain rules it only approximates.
// 1.18+: desert pyramids (21x21) and jungle temples (12x15) need solid ground at y >= sea level (63)
// at all four corners of their footprint, measured from the chunk's minimum corner (as SinglePieceStructure does).
// `tn` must be set up for the same version and seed (Overworld); it may be NULL for versions before 1.18.
int sfViableStructure(int structType, Generator *g, TerrainNoise *tn, int x, int z);

#endif
