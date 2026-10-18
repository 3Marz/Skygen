#ifndef SKYGEN_MAP_GEN_H
#define SKYGEN_MAP_GEN_H

#include <raylib.h>

/* Capacity of the per-vertex adjacency arrays. Must exceed the largest
 * Delaunay degree / Voronoi-vertex degree in the map; overflow is counted in
 * MapData.clamped instead of corrupting memory. */
#define MAXN 64

/* Internal divide-and-conquer hull scratch. Only ever referenced through a
 * pointer here; the full definition lives in map_gen.c. */
typedef struct HullEdge HullEdge;

typedef struct {
  Vector2 map_size; /* map rectangle in pixels */
  int n_sites;      /* number of polygon centers to seed */
  int relax;        /* Lloyd relaxation iterations (0 = none) */
} Params;

/* A site (polygon center). neigh/edges/corners describe its cell. */
typedef struct {
  Vector2 pos;
  int border, water, ocean, coast, biome;
  float elevation, moisture;    /* map elevation 0-100 and moisture */
  int nneigh, neigh[MAXN];      /* neighbouring centers sharing a visible border
                                 * inside the map (the map's Voronoi graph).    */
  /* All Delaunay neighbours, including those whose shared Voronoi edge fell
   * outside the map. Only point_in_cell() needs these: a clipped cell is the
   * intersection of the half-planes of its VISIBLE neighbours, but a rectangle
   * corner can still sit on the wrong side of an off-map bisector, so the full
   * set is required to test corners exactly. */
  int nfneigh, fneigh[MAXN];
  int nedges, edges[MAXN];      /* edges bordering this polygon             */
  int ncorners, corners[MAXN];  /* polygon corners, sorted by angle         */
} Center;

/* A Voronoi vertex / Delaunay triangle ("blue point").
 * touches are the centers meeting here, prot the edges leaving this corner and
 * adj the neighbouring corners. The degree of a Voronoi vertex is unbounded
 * (border corners, cocircular hubs), so these are MAXN-capped, not [3]. */
typedef struct {
  Vector2 pos;
  int border, water, ocean, coast, river, downslope;
  float elevation, moisture;
  int ntouch, touches[MAXN];    /* the centers meeting here                 */
  int nadj, adj[MAXN];          /* adjacent corners (Voronoi edges)         */
  int nprot, prot[MAXN];        /* edges leaving this corner                */
} Corner;

/* One edge knows both graphs: two centers (d0,d1) and two corners (v0,v1) */
typedef struct {
  int d0, d1, v0, v1;
  int river;
  Vector2 mid;
} Edge;

typedef struct {
  int nc, nq, ne;
  Center *c;
  Corner *q;
  Edge *e;
  float spacing;                /* average distance between centers         */
  /* polyline (v0 .. v1) of every edge, straight or noisy */
  Vector2 *pool;
  int poolN, poolCap;
  int *pstart, *plen;

  /* Working state, not part of the map graph */
  Vector2 texture_size;
  int *tris;                    /* Delaunay triangles (CCW site triples)   */
  int n_tris, cap_tris;
  int cap_c;                    /* allocated capacity of c[]              */
  HullEdge **howned;            /* recursion scratch rings, for cleanup   */
  int n_howned, cap_howned;
  int clamped;                  /* adjacencies dropped because of MAXN      */
  int *eidx;                    /* directed-edge -> triangle hash, -1/-2   */
  unsigned long long *ekey;
  int eicap, eitomb;
  int ecap;                     /* capacity of e[]                          */

  /* Cached rendering. Rebuilt only when the map changes, since filling and
   * uploading every frame is pure waste for a static map. */
  Texture2D tex;
  int dirty;

  // For heightmap
  float blob_power;
} MapData;

/* ------------------------------------------------------------------------- */
/* Public API                                                                 */
/* ------------------------------------------------------------------------- */

/* Allocate an empty map sized for p and seed its centers. */
MapData *init_voronoi_map(Params p);

/* Free the map and everything it owns. */
void free_voronoi_map(MapData *map);

/* Reseed the centers, triangulate, relax and generate the heightmap. */
void generate_voronoi_map(MapData *map, Params p);

/* Run `iterations` Lloyd steps (moves centers to their cell centroids). */
void relax_voronoi_map(MapData *map, int iterations);

/* Recompute every cell's elevation from the random hills. */
void generate_heightmap(MapData *map);

/* Index of the center nearest to p, or -1 if the map has no centers. */
int get_closest_center_index(MapData *map, Vector2 p);

/* Rasterise the coloured map into a fresh Image (caller owns the data). */
Image gen_voronoi_image(MapData *map);

/* Upload genVoronoiImage() to a Texture2D (cached until the map changes). */
Texture2D gen_voronoi_texture(MapData *map);

/* Draw the cached map texture (optionally with cell outlines). */
void draw_voronoi_map(MapData *map, int with_lines);

#endif /* SKYGEN_MAP_GEN_H */
