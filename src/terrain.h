#ifndef SKYGEN_TERRAIN_H
#define SKYGEN_TERRAIN_H

#include "map_gen.h"
#include <raylib.h>

typedef struct {

  float spacing;
  Vector2 size; // number of verts along x and y

  Vector3 **vert;

} TGrid;

typedef struct {

  Texture height_map;

  TGrid grid;
  Model model;

} Terrain;

void tgrid_init(TGrid *grid, Vector2 size, float spacing);
void tgrid_generate(TGrid *grid, MapGraph *map);
void tgrid_free(TGrid *grid);

void terrain_init(Terrain *terr);
void terrain_generate(Terrain *terr, MapGraph *map);
void terrain_draw(Terrain *terr);
void terrain_free(Terrain *terr);

#endif
