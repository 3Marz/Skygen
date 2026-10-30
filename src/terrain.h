#ifndef SKYGEN_TERRAIN_H
#define SKYGEN_TERRAIN_H

#include "map_gen.h"
#include <raylib.h>

typedef struct {

  Texture height_map;

  Model model;

} Terrain;

void terrain_init(Terrain *terr);
void terrain_generate(Terrain *terr, MapGraph *map);
void terrain_draw(Terrain *terr);
void terrain_free(Terrain *terr);

#endif
