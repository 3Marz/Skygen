#ifndef SKYGEN_MAP_GEN_HEIGHT_H
#define SKYGEN_MAP_GEN_HEIGHT_H

#include "map_gen.h"
#include "utils.h"

/* Assign the heights of the map from a set of random types of maps. */
void map_heights_assign(MapGraph *map);

/* Continuous terrain height (0..1) at map position pos, interpolated
 * barycentrically over the Delaunay triangulation of the centers. */
float map_heights_evaluate(MapGraph *map, Vector2 pos);

#endif
