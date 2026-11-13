#ifndef SKYGEN_TERRAIN_H
#define SKYGEN_TERRAIN_H

#include "map_gen.h"
#include <raylib.h>

#define LOD0_GEO 128 
#define LOD0_GE1 64
#define LOD0_GE2 32

// #define VIEW_DIST_LOD0 30.0f*10000.0f
// #define VIEW_DIST_LOD1 30.0f*10000.0f
// #define VIEW_DIST_LOD2 60.0f*10000.0f

#define VIEW_DIST_LOD0 1
#define VIEW_DIST_LOD1 2
#define VIEW_DIST_LOD2 4

#define CHUNCK_SPACE 256
#define CHUNCK_CUT_FALLOFF 10

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

} TChunk;

/* Smooth float terrain heights, sampled from the map at `step` pixels and
 * blurred so the piecewise-planar Delaunay interpolation does not show up as
 * flat triangles in the terrain shading. */
typedef struct {
  float *data;
  int width, height;
  int step;
} THeightField;

typedef struct {

  Shader *light_shader;

  THeightField height_field;

  TChunk *chunk;
  Vector2 nchunk;

  Vector2 last_chunk;

} Terrain;

void terrain_init(Terrain *terr, Shader *light_shader);
void terrain_clear(Terrain *terr);
void terrain_generate(Terrain *terr, MapGraph *map, int seed, Vector3 inital_cam_pos);
void terrain_update(Terrain *terr, MapGraph *map, Vector3 cam_pos);
void terrain_draw(Terrain *terr);
void terrain_free(Terrain *terr);

#endif
