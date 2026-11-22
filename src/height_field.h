#ifndef SKYGEN_HEIGHT_FIELD_H
#define SKYGEN_HEIGHT_FIELD_H

#include "map_gen.h"
#include <raylib.h>

/* Smooth float terrain heights, sampled from the map at `step` pixels and
 * blurred so the piecewise-planar Delaunay interpolation does not show up as
 * flat triangles in the terrain shading. */
typedef struct {
  float *data;
  int width, height;
  int step;
} THeightField;

void height_field_free(THeightField *field);
void height_field_build(THeightField *field, MapGraph *map);

/* Bilinear sample of the cached field at a map-space position. */
float height_field_sample(const THeightField *field, float x, float y);

/* Normal of the terrain at a map-space position. The stencil is fixed in world
 * units, so every chunk and every LOD gets the same normal wherever they share
 * a vertex. That is what keeps lighting continuous across LOD seams. */
Vector3 height_field_normal(const THeightField *field, float x, float y,
                            float height_power);

#endif
