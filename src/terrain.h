#ifndef SKYGEN_TERRAIN_H
#define SKYGEN_TERRAIN_H

#include "height_field.h"
#include "map_gen.h"
#include <raylib.h>

#define LOD_GEO0 64
#define LOD_GEO1 32
#define LOD_GEO2 16
#define LOD_GEO3 8

#define VIEW_DIST_LOD0 2
#define VIEW_DIST_LOD1 4
#define VIEW_DIST_LOD2 6
#define VIEW_DIST_LOD3 10

#define CHUNK_SPACE 128
#define CHUNK_CUT_FALLOFF 10
#define CHUNK_HEIGHTPOWER 450

const int LOD_GEOS[4] = {LOD_GEO0, LOD_GEO1, LOD_GEO2, LOD_GEO3};
const int nLOD_GEOS = sizeof(LOD_GEOS) / sizeof(LOD_GEOS[0]);
const int VIEW_DISTS[4] = {VIEW_DIST_LOD0, VIEW_DIST_LOD1, VIEW_DIST_LOD2, VIEW_DIST_LOD3};
const int nVIEW_DISTS = sizeof(VIEW_DISTS) / sizeof(VIEW_DISTS[0]);

enum TCHUNK_NEIGHBOR_LOD {
  N_TOP,
  N_BOTTOM,
  N_LEFT,
  N_RIGHT,
};

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

  int lod_level;
  int neigh_lods[4];

} TChunk;

typedef struct {

  Shader *light_shader;

  THeightField height_field;

  TChunk **chunk;
  Vector2 nchunk;

  Vector2 last_chunk;

  bool wire_draw;

} Terrain;

void terrain_init(Terrain *terr, Shader *light_shader);
void terrain_clear(Terrain *terr);
void terrain_generate(Terrain *terr, MapGraph *map, int seed, Vector3 inital_cam_pos);
void terrain_update(Terrain *terr, MapGraph *map, Vector3 cam_pos);
void terrain_draw(Terrain *terr);
void terrain_free(Terrain *terr);

#endif
