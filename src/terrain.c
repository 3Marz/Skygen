
#include "terrain.h"
#include "generation_ctx.h"
#include "map_gen.h"
#include "map_gen_height.h"
#include "utils.h"
#include "../include/hashmap.h"
#include <raylib.h>
#include <raymath.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>


/* The terrain mesh samples a cached float field instead of evaluating the
 * Delaunay interpolant at every vertex. Sampling every 2 map pixels keeps the
 * cache small; the blur hides the C0 creases of the barycentric field, which
 * would otherwise make every Delaunay triangle a flat-shaded plane. */
#define HEIGHT_FIELD_STEP 2
#define HEIGHT_FIELD_BLUR_SCALE 0.10f /* blur sigma as a fraction of site spacing */
#define HEIGHT_FIELD_BLUR_PASSES 2

static void height_field_free(THeightField *field) {
  if (!field) return;
  free(field->data);
  field->data = NULL;
  field->width = 0;
  field->height = 0;
  field->step = 0;
}

/* Separable box blur with clamped edges, so the border keeps its value. */
static void height_field_box_blur(float *data, float *tmp, int w, int h, int radius) {
  int span = 2 * radius + 1;

  for (int y = 0; y < h; y++) {
    float *row = &data[(size_t)y * w];
    float sum = 0.0f;
    for (int x = -radius; x <= radius; x++) {
      int sx = x < 0 ? 0 : (x >= w ? w - 1 : x);
      sum += row[sx];
    }
    for (int x = 0; x < w; x++) {
      tmp[(size_t)y * w + x] = sum / span;
      int out = x - radius;
      int in = x + radius + 1;
      if (out < 0) out = 0;
      if (in >= w) in = w - 1;
      sum += row[in] - row[out];
    }
  }

  for (int x = 0; x < w; x++) {
    float sum = 0.0f;
    for (int y = -radius; y <= radius; y++) {
      int sy = y < 0 ? 0 : (y >= h ? h - 1 : y);
      sum += tmp[(size_t)sy * w + x];
    }
    for (int y = 0; y < h; y++) {
      data[(size_t)y * w + x] = sum / span;
      int out = y - radius;
      int in = y + radius + 1;
      if (out < 0) out = 0;
      if (in >= h) in = h - 1;
      sum += tmp[(size_t)in * w + x] - tmp[(size_t)out * w + x];
    }
  }
}

/* Rasterise the map height field once (instead of per chunk) and smooth it. */
static void height_field_build(THeightField *field, MapGraph *map) {
  height_field_free(field);
  if (!map || map->nc <= 0) return;

  int step = HEIGHT_FIELD_STEP;
  int w = (int)(map->texture_size.x / step) + 1;
  int h = (int)(map->texture_size.y / step) + 1;
  if (w < 2 || h < 2) return;

  float *data = (float *)malloc((size_t)w * h * sizeof(float));
  if (!data) return;

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      data[(size_t)y * w + x] = map_heights_evaluate(
          map, (Vector2){ (float)(x * step), (float)(y * step) });
    }
  }

  float *tmp = (float *)malloc((size_t)w * h * sizeof(float));
  if (tmp) {
    /* Keep the blur proportional to the Delaunay triangle size, so the field
     * stays smooth when the user changes the number of sites. Two box passes
     * give the kernel a C1 profile. */
    int radius = (int)(map->spacing * HEIGHT_FIELD_BLUR_SCALE / step + 0.5f);
    if (radius < 1) radius = 1;

    for (int p = 0; p < HEIGHT_FIELD_BLUR_PASSES; p++)
      height_field_box_blur(data, tmp, w, h, radius);
    free(tmp);
  }

  field->data = data;
  field->width = w;
  field->height = h;
  field->step = step;
}

/* Bilinear sample of the cached field at a map-space position. */
static float height_field_sample(const THeightField *field, float x, float y) {
  if (!field || !field->data || field->width < 2 || field->height < 2 ||
      field->step <= 0)
    return 0.0f;

  float fx = x / field->step;
  float fy = y / field->step;
  int x0 = (int)floorf(fx);
  int y0 = (int)floorf(fy);
  float tx = fx - (float)x0;
  float ty = fy - (float)y0;
  int x1 = x0 + 1;
  int y1 = y0 + 1;

  if (x0 < 0) x0 = 0; else if (x0 >= field->width) x0 = field->width - 1;
  if (x1 < 0) x1 = 0; else if (x1 >= field->width) x1 = field->width - 1;
  if (y0 < 0) y0 = 0; else if (y0 >= field->height) y0 = field->height - 1;
  if (y1 < 0) y1 = 0; else if (y1 >= field->height) y1 = field->height - 1;

  float a = field->data[(size_t)y0 * field->width + x0];
  float b = field->data[(size_t)y0 * field->width + x1];
  float c = field->data[(size_t)y1 * field->width + x0];
  float d = field->data[(size_t)y1 * field->width + x1];

  float top = a + (b - a) * tx;
  float bottom = c + (d - c) * tx;
  return top + (bottom - top) * ty;
}

void tgrid_init(TGrid *grid, Vector2 size, float spacing) {

  //TODO Global height_power
  grid->height_power = 250.0f;
  // Add more size for edges seamless gaps bettwen chunks
  grid->size = v2(size.x+2, size.y+2);
  grid->spacing = spacing;

  grid->vert = (Vector3 **)malloc(grid->size.x * sizeof(Vector3 *));
  for (int i = 0; i < grid->size.x; i++) {
    grid->vert[i] = (Vector3 *)malloc(grid->size.y * sizeof(Vector3));
  }

}

void tgrid_generate(TGrid *grid, Vector2 offset, const THeightField *height_field) {
  if (!grid->vert) return;

  for (int x = 0; x < grid->size.x; x++) {
    for (int y = 0; y < grid->size.y; y++) {

      int px = x * grid->spacing; 
      int pz = y * grid->spacing; 

      float h = 0;
      float xof = 0;
      float zof = 0;

      // h = height_field_sample(height_field, (x+offset.x)*grid->spacing, (y+offset.y)*grid->spacing);
      // h *= grid->height_power;
      
      h = height_field_sample(height_field, ((x-1)+offset.x)*grid->spacing, ((y-1)+offset.y)*grid->spacing);
      h *= grid->height_power;

      if (x > 0 && x < grid->size.x-1 && y > 0 && y < grid->size.y-1) {
      } else {
        h -= CHUNK_CUT_FALLOFF;
        if (x == 0)              xof = grid->spacing;
        if (x == grid->size.x-1) xof = -grid->spacing;
        if (y == 0)              zof = grid->spacing;
        if (y == grid->size.y-1) zof = -grid->spacing;
      }

      grid->vert[x][y] = (Vector3){(px+xof)-grid->spacing, h, (pz+zof)-grid->spacing};
    }
  }

}

void tgrid_free(TGrid *grid) {
  if (!grid || !grid->vert) {
    if (grid) {
      grid->size = (Vector2){ 0 };
      grid->spacing = 0.0f;
      grid->height_power = 0.0f;
    }
    return;
  }
  for (int i = 0; i < grid->size.x; i++) {
    free(grid->vert[i]);
    grid->vert[i] = NULL;
  }
  free(grid->vert);
  grid->vert = NULL;
  grid->size = (Vector2){ 0 };
  grid->spacing = 0.0f;
  grid->height_power = 0.0f;
}

void tmesh_set_vert(Mesh *tm, int i, Vector3 p) {
  tm->vertices[i*3    ] = p.x;
  tm->vertices[i*3 + 1] = p.y;
  tm->vertices[i*3 + 2] = p.z;
}
void tmesh_add_normal(Mesh *tm, int i, Vector3 p) {
  tm->normals[i*3    ] += p.x;
  tm->normals[i*3 + 1] += p.y;
  tm->normals[i*3 + 2] += p.z;
}
void tmesh_set_normal(Mesh *tm, int i, Vector3 p) {
  tm->normals[i*3    ] = p.x;
  tm->normals[i*3 + 1] = p.y;
  tm->normals[i*3 + 2] = p.z;
}
void tmesh_set_texcord(Mesh *tm, int i, float x, float y) {
  tm->texcoords[i*2    ] = x;
  tm->texcoords[i*2 + 1] = y;
}

void tchunk_init(TChunk *chunk, Vector3 position) {
  chunk->model = (Model){ 0 };
  chunk->grid = (TGrid){ 0 };
  chunk->position = position;
  chunk->is_active = true;
  chunk->lod_level = -1;
}

void tchunk_free(TChunk *chunk) {
  if (IsModelValid(chunk->model))
    UnloadModel(chunk->model);
  tgrid_free(&chunk->grid);
}

void tchunk_generate(TChunk *chunk, const THeightField *height_field, Shader *light_shader, int lod_level) {
  if (lod_level == chunk->lod_level) return;

  tchunk_free(chunk);
  chunk->lod_level = lod_level;
  if (!height_field || !height_field->data) return;

  int lods[3] = {LOD0_GEO, LOD0_GE1, LOD0_GE2};
  float s = lods[lod_level];
  tgrid_init(&chunk->grid, (Vector2){s+1, s+1}, CHUNK_SPACE/(s));
  tgrid_generate(&chunk->grid, 
      v2(chunk->position.x/(CHUNK_SPACE/(s)), chunk->position.z/(CHUNK_SPACE/(s))),
      height_field);

  int width = chunk->grid.size.x,
      height = chunk->grid.size.y;

  Mesh tm = {0};
  tm.triangleCount = 2 * ((width-1) * (height-1));
  if (tm.triangleCount <= 0)
    return;
  // tm.vertexCount = tm.triangleCount * 3;
  tm.vertexCount = width * height;

  tm.vertices = (float *)MemAlloc(tm.vertexCount * 3 * sizeof(float));
  tm.normals = (float *)MemAlloc(tm.vertexCount * 3 * sizeof(float));
  tm.texcoords = (float *)MemAlloc(tm.vertexCount * 2 * sizeof(float));
  tm.indices = (unsigned short *)MemAlloc( ((width-1) * (height-1) * 6) * sizeof(unsigned short));

  // init verts
  for (int x = 0; x < width; x++) {
    for (int y = 0; y < height; y++) {

      int i = x * height + y; 
      Vector3 point = chunk->grid.vert[x][y];

      tmesh_set_vert(&tm, i, point);
      tmesh_set_normal(&tm, i, Vector3Zero());

      tmesh_set_texcord(&tm, i, 
        (float)x / (width - 1),
        (float)y / (height - 1)
      );

    }
  }

  int vi = 0;
  for (int x = 0; x < width - 1; x++) {
    for (int y = 0; y < height - 1; y++) {

      int a = x * height + y;
      int b = x * height + (y + 1);
      int c = (x + 1) * height + y;
      int d = (x + 1) * height + (y + 1);

      // First triangle: A, B, C
      tm.indices[vi++] = a;
      tm.indices[vi++] = b;
      tm.indices[vi++] = c;

      // Second triangle: B, D, C
      tm.indices[vi++] = b;
      tm.indices[vi++] = d;
      tm.indices[vi++] = c;

      // First triangle normal
      Vector3 edgeA = Vector3Subtract(
          chunk->grid.vert[x][y + 1],
          chunk->grid.vert[x][y]
      );

      Vector3 edgeB = Vector3Subtract(
          chunk->grid.vert[x + 1][y],
          chunk->grid.vert[x][y]
      );

      Vector3 normal1 = Vector3CrossProduct(edgeA, edgeB);

      if (x == 0 || x == width - 2 || y == 0 || y == height - 2) {
        tmesh_set_normal(&tm, a, normal1);
        tmesh_set_normal(&tm, b, normal1);
        tmesh_set_normal(&tm, c, normal1);
      } else {
        tmesh_add_normal(&tm, a, normal1);
        tmesh_add_normal(&tm, b, normal1);
        tmesh_add_normal(&tm, c, normal1);
      }

      // Second triangle normal
      Vector3 edgeA2 = Vector3Subtract(
          chunk->grid.vert[x + 1][y + 1],
          chunk->grid.vert[x][y + 1]
      );

      Vector3 edgeB2 = Vector3Subtract(
          chunk->grid.vert[x + 1][y],
          chunk->grid.vert[x][y + 1]
      );

      Vector3 normal2 = Vector3CrossProduct(edgeA2, edgeB2);

      if (x == 0 || x == width - 2 || y == 0 || y == height - 2) {
        tmesh_set_normal(&tm, b, normal2);
        tmesh_set_normal(&tm, d, normal2);
        tmesh_set_normal(&tm, c, normal2);
      } else {
        tmesh_add_normal(&tm, b, normal2);
        tmesh_add_normal(&tm, d, normal2);
        tmesh_add_normal(&tm, c, normal2);
      }

    }
  }

  for (int i = 0; i < tm.vertexCount; i++) {
    Vector3 v_normal = v3(tm.normals[i * 3], tm.normals[i * 3+1], tm.normals[i * 3+2]); 
    tmesh_set_normal(&tm, i, Vector3Normalize(v_normal));
  }

  UploadMesh(&tm, false);

  chunk->model = LoadModelFromMesh(tm);
  chunk->model.materials[0].shader = *light_shader;
}

TChunk* get_chunk(Terrain *terr, int x, int y) {
  x = clamp(x, 0, terr->nchunk.x -1);
  y = clamp(y, 0, terr->nchunk.y -1);
  int idx = x * (int)terr->nchunk.x + y;
  return &terr->chunk[idx];
}

void terrain_init(Terrain *terr, Shader *light_shader) {

  terr->light_shader = light_shader;
  terr->nchunk = (Vector2){
    (float)MAP_SIZE_X/CHUNK_SPACE, (float)MAP_SIZE_Y/CHUNK_SPACE
  };
  terr->last_chunk = v2(-1, -1);

  terr->chunk = (TChunk *)malloc(terr->nchunk.x * terr->nchunk.y * sizeof(TChunk));

  for (int x = 0; x < terr->nchunk.x; x++) {
    for (int y = 0; y < terr->nchunk.y; y++) {
      tchunk_init(get_chunk(terr, x, y), (Vector3){x*CHUNK_SPACE, 0, y*CHUNK_SPACE});
    }
  }

}

void terrain_clear(Terrain *terr) {
  if (!terr) return;
  height_field_free(&terr->height_field);
  if (!terr->chunk) return;

  for (int x = 0; x < terr->nchunk.x; x++) {
    for (int y = 0; y < terr->nchunk.y; y++) {
      TChunk *c = get_chunk(terr, x, y);
      Vector3 position = c->position;
      tchunk_free(c);
      tchunk_init(c, position);
    }
  }
}

static Vector2 get_chunk_idx_v(Terrain *terr, Vector3 cam_pos) {
  int x = floorf(cam_pos.x / CHUNK_SPACE);
  int y = floorf(cam_pos.z / CHUNK_SPACE);
  return v2(x, y);
}

// hashmap for chunks ID and LOD LEVEL to load
typedef struct {
  int id;
  int lod_level;
} ChunkLoad;

int cl_compare(const void *a, const void *b, void *udata) {
  const ChunkLoad *ca = a;
  const ChunkLoad *cb = b;
  return ca->id == cb->id;
}
uint64_t cl_hash(const void *item, uint64_t seed0, uint64_t seed1) {
  const ChunkLoad *cl = item;
  return hashmap_sip(&cl->id, sizeof(int), seed0, seed1);
}

void update_chunks(Terrain *terr, Vector2 new_chunk) {

  struct hashmap *map = hashmap_new(sizeof(ChunkLoad), 0, 0, 0, 
                                    cl_hash, cl_compare, NULL, NULL);

  for (int i = -VIEW_DIST_LOD2; i <= VIEW_DIST_LOD2; i++) {
    for (int j = -VIEW_DIST_LOD2; j <= VIEW_DIST_LOD2; j++) {
      int x = clamp(new_chunk.x + i, 0, terr->nchunk.x - 1);
      int y = clamp(new_chunk.y + j, 0, terr->nchunk.y - 1);
      int id = x * (int)terr->nchunk.x + y;
      hashmap_set(map, &(ChunkLoad){.id = id, .lod_level = 2});
    }
  }

  for (int i = -VIEW_DIST_LOD1; i <= VIEW_DIST_LOD1; i++) {
    for (int j = -VIEW_DIST_LOD1; j <= VIEW_DIST_LOD1; j++) {
      int x = clamp(new_chunk.x + i, 0, terr->nchunk.x - 1);
      int y = clamp(new_chunk.y + j, 0, terr->nchunk.y - 1);
      int id = x * (int)terr->nchunk.x + y;
      hashmap_set(map, &(ChunkLoad){.id = id, .lod_level = 1});
    }
  }

  for (int i = -VIEW_DIST_LOD0; i <= VIEW_DIST_LOD0; i++) {
    for (int j = -VIEW_DIST_LOD0; j <= VIEW_DIST_LOD0; j++) {
      int x = clamp(new_chunk.x + i, 0, terr->nchunk.x - 1);
      int y = clamp(new_chunk.y + j, 0, terr->nchunk.y - 1);
      int id = x * (int)terr->nchunk.x + y;
      hashmap_set(map, &(ChunkLoad){.id = id, .lod_level = 0});
    }
  }

  size_t iter = 0;
  void *item;
  while (hashmap_iter(map, &iter, &item)) {
    const ChunkLoad *cl = item;
    TChunk *c = &terr->chunk[cl->id];
    if (c == NULL) return;
    tchunk_generate(c, &terr->height_field, terr->light_shader, cl->lod_level);
  }

  hashmap_free(map);
}

void terrain_generate(Terrain *terr, MapGraph *map, int seed, Vector3 inital_cam_pos) {
  (void)seed; /* the PNG height seed is no longer used; the map is the source */

  height_field_build(&terr->height_field, map);
  if (!terr->height_field.data) return;

  Vector2 curr_chunk = get_chunk_idx_v(terr, inital_cam_pos);

  if (!Vector2Equals(curr_chunk, terr->last_chunk)) {
    update_chunks(terr, curr_chunk);
    terr->last_chunk = curr_chunk;
  }

  // for (int x = 0; x < terr->nchunk.x; x++) {
  //   for (int y = 0; y < terr->nchunk.y; y++) {
  //     TChunk *c = get_chunk(terr, x, y);
  //     Vector2 ch_pos2 = {c->position.x, c->position.z};
  //     Vector2 cm_pos2 = {inital_cam_pos.x, inital_cam_pos.z};
  //     float dist = Vector2DistanceSqr(ch_pos2, cm_pos2);
  //     int lod = -1;
  //
  //     if (dist > VIEW_DIST_LOD2){
  //       lod = 2;
  //       c->is_active = false;
  //   }
  //     else if (dist > VIEW_DIST_LOD1){
  //       lod = 1;
  //       c->is_active = false;
  //     }
  //     else {
  //       lod = 0;
  //       c->is_active = true;
  //     }
  //
  //     if (c->is_active)
  //       tchunk_generate(c, &terr->height_field, terr->light_shader, lod);
  //
  //     // c->is_active = true;
  //   }
  // }
}

void terrain_update(Terrain *terr, MapGraph *map, Vector3 cam_pos) {
  if (!terr->height_field.data)
    height_field_build(&terr->height_field, map);

  Vector2 curr_chunk = get_chunk_idx_v(terr, cam_pos);

  if (!Vector2Equals(curr_chunk, terr->last_chunk)) {
    update_chunks(terr, curr_chunk);
    terr->last_chunk = curr_chunk;
  }

  // for (int x = 0; x < terr->nchunk.x; x++) {
  //   for (int y = 0; y < terr->nchunk.y; y++) {
  //     TChunk *c = get_chunk(terr, x, y);
  //     Vector2 ch_pos2 = {
  //       c->position.x,//+((c->grid.size.x*c->grid.spacing)/2),
  //       c->position.z //+((c->grid.size.y*c->grid.spacing)/2)
  //     };
  //     Vector2 cm_pos2 = {cam_pos.x, cam_pos.z};
  //
  //     float dist = Vector2DistanceSqr(ch_pos2, cm_pos2);
  //     int lod = -1;
  //
  //     if (dist > VIEW_DIST_LOD2)
  //       lod = 2;
  //     else if (dist > VIEW_DIST_LOD1)
  //       lod = 1;
  //     else
  //       lod = 0;
  //
  //     // if (!IsModelValid(c->model) && terr->height_field.data)
  //     if (terr->height_field.data)
  //       tchunk_generate(c, &terr->height_field, terr->light_shader, lod);
  //
  //   }
  // }
}

void terrain_draw(Terrain *terr) {

  for (int x = 0; x < terr->nchunk.x; x++) {
    for (int y = 0; y < terr->nchunk.y; y++) {

      TChunk *c = get_chunk(terr, x, y);
      if (c->is_active && IsModelValid(c->model)) {
        DrawModelWires(c->model, c->position, 1.0f, (Color){61, 245, 73, 225});
      }
    }
  }

}

void terrain_free(Terrain *terr) {

  for (int i = 0; i < terr->nchunk.x * terr->nchunk.y; i++) {
    tchunk_free(&terr->chunk[i]);
  }
  free(terr->chunk); terr->chunk = NULL;

  height_field_free(&terr->height_field);

}

