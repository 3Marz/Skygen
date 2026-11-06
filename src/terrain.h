#ifndef SKYGEN_TERRAIN_H
#define SKYGEN_TERRAIN_H

#include "map_gen.h"
#include <raylib.h>

typedef struct {

  float height_power;
  float spacing; // spacing bettwen verts
  Vector2 size; // number of verts along x and y

  Vector3 **vert;

} TGrid;

typedef struct {

  Vector3 position;
  TGrid grid;
  Model model;
  bool is_active;

} TChunk;

typedef struct {

  Image height_map;

  TChunk **chunk;
  Vector2 nchunk;

} Terrain;

void tgrid_init(TGrid *grid, Vector2 size, float spacing);
void tgrid_generate(TGrid *grid, Vector2 offset, Image height_map);
void tgrid_free(TGrid *grid);

void tchunk_init(TChunk *chunk, Vector3 position);
void tchunk_generate(TChunk *chunk, Image height_map);
void tchunk_free(TChunk *chunk);

void terrain_init(Terrain *terr);
void terrain_generate(Terrain *terr, MapGraph *map, int seed);
void terrain_update(Terrain *terr, Vector3 cam_pos);
void terrain_draw(Terrain *terr);
void terrain_free(Terrain *terr);

#endif
