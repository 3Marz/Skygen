
#include "terrain.h"
#include "generation_ctx.h"
#include "height_field.h"
#include "map_gen.h"
#include "utils.h"
#include "../include/r3d/r3d_texture.h"
#include "../include/hashmap.h"
#include <raylib.h>
#include <raymath.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


void tgrid_init(TGrid *grid, Vector2 size, float spacing) {

  grid->height_power = CHUNK_HEIGHTPOWER;
  grid->size = v2(size.x, size.y);
  grid->spacing = spacing;

  grid->vert = (Vector3 **)malloc(grid->size.x * sizeof(Vector3 *));
  for (int i = 0; i < grid->size.x; i++) {
    grid->vert[i] = (Vector3 *)malloc(grid->size.y * sizeof(Vector3));
  }

}

void tgrid_generate(TGrid *grid, Vector2 offset, const THeightField *height_field, int curr_lod, int neigh_lods[4]) {
  if (!grid->vert) return;

  Vector2 left_side[(int)grid->size.y]  ; int li = 0;
  Vector2 right_side[(int)grid->size.y] ; int ri = 0;
  Vector2 top_side[(int)grid->size.x]   ; int ti = 0;
  Vector2 bottom_side[(int)grid->size.x]; int bi = 0;

  for (int x = 0; x < grid->size.x; x++) {
    for (int y = 0; y < grid->size.y; y++) {

      int px = x * grid->spacing; 
      int pz = y * grid->spacing; 

      float h = height_field_sample(height_field, (x+offset.x)*grid->spacing, (y+offset.y)*grid->spacing);
      h *= grid->height_power;
      
      if (y <= 0)
        top_side[ti++] = v2(x, y);    //TOP

      if (y >= grid->size.y-1)
        bottom_side[bi++] = v2(x, y); //BOTTOM

      if (x <= 0)
        left_side[li++] = v2(x, y);   //LEFT

      if (x >= grid->size.x-1)
        right_side[ri++] = v2(x, y);  //RIGHT


      grid->vert[x][y] = (Vector3){px, h, pz};
    }
  }

  int sides[4] = {N_TOP, N_BOTTOM, N_LEFT, N_RIGHT};
  for (int s = 0; s < 4; s++) {
    const int SIDE = sides[s];
    if (neigh_lods[SIDE] != -1 && neigh_lods[SIDE] > curr_lod) {

      int itr_size = s > 1 ? grid->size.y : grid->size.x; // iter size for top/bottom or left/right;

      for (int i = 0; i < itr_size; i++) {
        if (i % 2 == 0) continue;

        Vector2 id;
        switch (s) {
          case N_TOP:
            id = top_side[i]; break;
          case N_BOTTOM:
            id = bottom_side[i]; break;
          case N_LEFT:
            id = left_side[i]; break;
          case N_RIGHT:
            id = right_side[i]; break;
        }
        int x = (int)id.x,
            y = (int)id.y;

        Vector3 old_v = grid->vert[x][y];
        float left_h, right_h;
        if (s > 1) { // Left/Right side
          left_h = grid->vert[x][y-1].y,
          right_h = grid->vert[x][y+1].y;
        } else { // Top/Bottom side
          left_h = grid->vert[x-1][y].y,
          right_h = grid->vert[x+1][y].y;
        }

        float avg_h = (left_h + right_h) / 2;

        grid->vert[x][y] = (Vector3){old_v.x, avg_h, old_v.z};

      }
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

void tchunk_init(TChunk *chunk, Vector3 position) {
  chunk->mesh = (R3D_Mesh){ 0 };
  chunk->grid = (TGrid){ 0 };
  chunk->position = position;
  chunk->is_active = false;
  chunk->lod_level = -1;

  int nl[4] = {-1, -1, -1, -1};
  memcpy(&chunk->neigh_lods, &nl, sizeof(nl));
}

void tchunk_free(TChunk *chunk) {
  if (R3D_IsMeshValid(chunk->mesh))
    R3D_UnloadMesh(chunk->mesh);
  chunk->mesh = (R3D_Mesh){ 0 };
  tgrid_free(&chunk->grid);
}

void tchunk_generate(TChunk *chunk, Terrain *terr, int lod_level) {

  tchunk_free(chunk);
  chunk->lod_level = lod_level;
  if (!terr || !terr->height_field.data) return;

  float geos[] = LOD_GEOS;
  float s = geos[lod_level];

  tgrid_init(&chunk->grid, (Vector2){s+1, s+1}, CHUNK_SPACE/(s));
  tgrid_generate(
    &chunk->grid, 
    v2(chunk->position.x/(CHUNK_SPACE/(s)), chunk->position.z/(CHUNK_SPACE/(s))),
    &terr->height_field,
    chunk->lod_level,
    chunk->neigh_lods
  );

  int width = chunk->grid.size.x,
      height = chunk->grid.size.y;
  int vertex_count = width * height;
  int index_count = (width - 1) * (height - 1) * 6;
  if (vertex_count <= 0 || index_count <= 0)
    return;

  /* Normals come from the shared height field, so a vertex on a chunk border
   * gets the same normal from both chunks no matter which LOD built it. */
  Vector3 *normals = (Vector3 *)malloc((size_t)vertex_count * sizeof(Vector3));
  if (!normals)
    return;

  for (int x = 0; x < width; x++) {
    for (int y = 0; y < height; y++) {
      int i = x * height + y;
      Vector3 point = chunk->grid.vert[x][y];
      normals[i] = height_field_normal(
          &terr->height_field,
          chunk->position.x + point.x,
          chunk->position.z + point.z,
          chunk->grid.height_power);
    }
  }

  /* On a stitched side the finer chunk pulls its odd border vertices onto the
   * midpoint of the coarse edge. Do the same for their normals, so both sides
   * interpolate the exact same normal along the shared edge. */
  int sides[4] = { N_TOP, N_BOTTOM, N_LEFT, N_RIGHT };
  for (int s = 0; s < 4; s++) {
    const int SIDE = sides[s];
    if (chunk->neigh_lods[SIDE] == -1 ||
        chunk->neigh_lods[SIDE] <= lod_level)
      continue;

    int itr_size = s > 1 ? height : width;
    for (int i = 1; i < itr_size; i += 2) {
      int x, y;
      switch (s) {
        case N_TOP:    x = i;                y = 0;            break;
        case N_BOTTOM: x = i;                y = height - 1;   break;
        case N_LEFT:   x = 0;                y = i;            break;
        default:       x = width - 1;        y = i;            break;
      }

      Vector3 n0, n1;
      if (s > 1) {
        n0 = normals[x * height + (y - 1)];
        n1 = normals[x * height + (y + 1)];
      } else {
        n0 = normals[(x - 1) * height + y];
        n1 = normals[(x + 1) * height + y];
      }

      normals[x * height + y] = Vector3Normalize(Vector3Add(n0, n1));
    }
  }

  R3D_MeshData data = R3D_LoadMeshData(vertex_count, index_count);
  if (!data.vertices || !data.indices) {
    free(normals);
    return;
  }

  data.vertexCount = vertex_count;
  data.indexCount = index_count;

  for (int x = 0; x < width; x++) {
    for (int y = 0; y < height; y++) {
      int i = x * height + y;
      Vector3 point = chunk->grid.vert[x][y];
      Vector2 uv = { (float)x / (width - 1), (float)y / (height - 1) };
      data.vertices[i] = R3D_MakeVertex(
          point, uv, normals[i], (Vector4){ 0.0f, 0.0f, 0.0f, 1.0f }, WHITE);
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
      data.indices[vi++] = a;
      data.indices[vi++] = b;
      data.indices[vi++] = c;

      // Second triangle: B, D, C
      data.indices[vi++] = b;
      data.indices[vi++] = d;
      data.indices[vi++] = c;

    }
  }

  R3D_GenMeshDataTangents(&data, R3D_PRIMITIVE_TRIANGLES);

  chunk->mesh = R3D_LoadMesh(R3D_PRIMITIVE_TRIANGLES, data, NULL);
  chunk->is_active = R3D_IsMeshValid(chunk->mesh);

  R3D_UnloadMeshData(data);
  free(normals);
}

TChunk* get_chunk(Terrain *terr, int x, int y) {
  if ((x >= 0 && x < terr->nchunk.x) && (y >= 0 && y < terr->nchunk.y))
    return &terr->chunk[x][y];

  return NULL;
}

static void terrain_material_init(Terrain *terr) {
  terr->terrain_shader = R3D_LoadSurfaceShader("assets/shaders/terrain_surface.glsl");

  terr->grass_texture = R3D_LoadTextureEx(
      "assets/textures/Grass/Grass005_1K-PNG_Color.png",
      TEXTURE_WRAP_REPEAT, TEXTURE_FILTER_ANISOTROPIC_4X, true);

  terr->rock_texture = R3D_LoadTextureEx(
      "assets/textures/Ground1/Ground099_1K-PNG_Color.png",
      TEXTURE_WRAP_REPEAT, TEXTURE_FILTER_ANISOTROPIC_4X, true);

  GenTextureMipmaps(&terr->grass_texture);
  GenTextureMipmaps(&terr->rock_texture);

  Vector2 uv_scale = v2(0.02f, 0.02f);
  Vector2 slope_range = v2(0.55f, 0.80f);
  Vector3 grass_tint = { 1.0f, 1.0f, 1.0f };
  Vector3 rock_tint = { 1.0f, 1.0f, 1.0f };

  R3D_SetSurfaceShaderUniform(terr->terrain_shader, "uvScale", &uv_scale);
  R3D_SetSurfaceShaderUniform(terr->terrain_shader, "slopeRange", &slope_range);
  R3D_SetSurfaceShaderUniform(terr->terrain_shader, "grassTint", &grass_tint);
  R3D_SetSurfaceShaderUniform(terr->terrain_shader, "rockTint", &rock_tint);
  R3D_SetSurfaceShaderSampler(terr->terrain_shader, "grassMap", terr->grass_texture);
  R3D_SetSurfaceShaderSampler(terr->terrain_shader, "rockMap", terr->rock_texture);

  terr->material = R3D_GetDefaultMaterial();
  terr->material.shader = terr->terrain_shader;
  terr->material.orm.roughness = 0.90f;
  terr->material.orm.metalness = 0.0f;
  terr->material.cullMode = R3D_CULL_BACK;
}

void terrain_init(Terrain *terr) {

  terr->nchunk = (Vector2){
    (float)MAP_SIZE_X/CHUNK_SPACE, (float)MAP_SIZE_Y/CHUNK_SPACE
  };
  terr->last_chunk = v2(-1, -1);

  terr->chunk = (TChunk **)malloc(terr->nchunk.x * sizeof(TChunk *));
  for (int i = 0; i < terr->nchunk.x; i++) {
    terr->chunk[i] = (TChunk *)malloc(terr->nchunk.y * sizeof(TChunk));
  }

  for (int x = 0; x < terr->nchunk.x; x++) {
    for (int y = 0; y < terr->nchunk.y; y++) {
      tchunk_init(&terr->chunk[x][y], (Vector3){x*CHUNK_SPACE, 0, y*CHUNK_SPACE});
    }
  }

  terrain_material_init(terr);
}

void terrain_clear(Terrain *terr) {
  if (!terr) return;
  height_field_free(&terr->height_field);
  terr->last_chunk = v2(-1, -1);
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
  Vector2 id;
  int lod_level;
} ChunkLoad;

int cl_compare(const void *a, const void *b, void *udata) {
  const ChunkLoad *ca = a;
  const ChunkLoad *cb = b;
  return Vector2Equals(ca->id, cb->id);
}
uint64_t cl_hash(const void *item, uint64_t seed0, uint64_t seed1) {
  const ChunkLoad *cl = item;
  return hashmap_sip(&cl->id.x, sizeof(int), seed0, seed1) + hashmap_xxhash3(&cl->id.y, sizeof(int), seed0, seed1);
}

bool vec_in_range(int range, Vector2 vec) {

  if (vec.x >= -range && vec.x <= range &&
      vec.y >= -range && vec.y <= range)
    return true;

  return false;
}

int lod_get(Vector2 size, int a[(int)size.x][(int)size.y], int x, int y) {
  if (x < 0 || x >= size.x || y < 0 || y >= size.y )
    return -1;
  return a[x][y];
}

void update_chunks(Terrain *terr, Vector2 new_chunk) {

  struct hashmap *map = hashmap_new(sizeof(ChunkLoad), 0, 0, 0, 
                                    cl_hash, cl_compare, NULL, NULL);

  int lod_levels[(int)terr->nchunk.x][(int)terr->nchunk.y];
  memset(lod_levels, -1, sizeof(int) * (int)terr->nchunk.x * (int)terr->nchunk.y);

  int view_dists[] = VIEW_DISTS;
  int max_view_dist = view_dists[arr_len(view_dists)-1];

  for (int i = -max_view_dist; i <= max_view_dist; i++) {
    for (int j = -max_view_dist; j <= max_view_dist; j++) {
      int x = new_chunk.x + i;
      int y = new_chunk.y + j;

      int lod_level = -1;
      Vector2 p = v2(i, j);

      if (vec_in_range(view_dists[0], p))
        lod_level = 0;
      else if (vec_in_range(view_dists[1], p))
        lod_level = 1;
      else if (vec_in_range(view_dists[2], p)){
        lod_level = 2;
      } else {
        lod_level = 3;
      }

      if (get_chunk(terr, x, y) == NULL) continue;

      lod_levels[x][y] = lod_level;

      hashmap_set(map, &(ChunkLoad){.id = v2(x, y), .lod_level = lod_level});
    }
  }

  for (int x = 0; x < terr->nchunk.x; x++) {
    for (int y = 0; y < terr->nchunk.y; y++) {
      TChunk *c = get_chunk(terr, x, y);

      c->is_active = false; // to only view generated chunks

      int t_lod = lod_get(terr->nchunk, lod_levels, x, y-1);
      int b_lod = lod_get(terr->nchunk, lod_levels, x, y+1);
      int l_lod = lod_get(terr->nchunk, lod_levels, x-1, y);
      int r_lod = lod_get(terr->nchunk, lod_levels, x+1, y);

      c->neigh_lods[N_TOP]    = t_lod;
      c->neigh_lods[N_BOTTOM] = b_lod;
      c->neigh_lods[N_LEFT]   = l_lod;
      c->neigh_lods[N_RIGHT]  = r_lod;
    }
  }

  size_t iter = 0;
  void *item;
  while (hashmap_iter(map, &iter, &item)) {
    const ChunkLoad *cl = item;
    TChunk *c = get_chunk(terr, cl->id.x, cl->id.y);
    if (c == NULL) continue;
    tchunk_generate(c, terr, cl->lod_level);
  }

  hashmap_free(map);
  //
  // TChunk c = terr->chunk[(int)new_chunk.x][(int)new_chunk.y];
  // printf("           top: %i,          \n", c.neigh_lods[N_TOP]);
  // printf("left: %i, curr: %i, right: %i\n", c.neigh_lods[N_RIGHT], c.lod_level, c.neigh_lods[N_LEFT]);
  // printf("        bottom: %i,          \n", c.neigh_lods[N_BOTTOM]);
}

void terrain_generate(Terrain *terr, MapGraph *map, int seed, Vector3 inital_cam_pos) {
  terrain_clear(terr);

  (void)seed; /* the PNG height seed is no longer used; the map is the source */

  height_field_build(&terr->height_field, map);
  if (!terr->height_field.data) return;

  Vector2 curr_chunk = get_chunk_idx_v(terr, inital_cam_pos);

  if (!Vector2Equals(curr_chunk, terr->last_chunk)) {
    update_chunks(terr, curr_chunk);
    terr->last_chunk = curr_chunk;
  }

}

void terrain_update(Terrain *terr, MapGraph *map, Vector3 cam_pos) {

  if (!terr->height_field.data)
    height_field_build(&terr->height_field, map);

  Vector2 curr_chunk = get_chunk_idx_v(terr, cam_pos);

  if (!Vector2Equals(curr_chunk, terr->last_chunk)) {
    update_chunks(terr, curr_chunk);
    terr->last_chunk = curr_chunk;
  }

}

void terrain_draw(Terrain *terr) {

  for (int x = 0; x < terr->nchunk.x; x++) {
    for (int y = 0; y < terr->nchunk.y; y++) {

      TChunk *c = get_chunk(terr, x, y);
      if (c->is_active && R3D_IsMeshValid(c->mesh)) {
        R3D_DrawMesh(c->mesh, terr->material, c->position, 1.0f);
      }
    }
  }

}

void terrain_free(Terrain *terr) {

  for (int i = 0; i < terr->nchunk.x; i++) {
    for (int j = 0; j < terr->nchunk.y; j++) {
      tchunk_free(&terr->chunk[i][j]);
    }
  }
  for (int i = 0; i < terr->nchunk.x; i++) {
    free(terr->chunk[i]); terr->chunk[i] = NULL;
  }
  free(terr->chunk); terr->chunk = NULL;

  height_field_free(&terr->height_field);

  if (terr->terrain_shader) {
    R3D_UnloadSurfaceShader(terr->terrain_shader);
    terr->terrain_shader = NULL;
  }

  if (terr->grass_texture.id > 0) {
    R3D_UnloadTexture(terr->grass_texture);
    terr->grass_texture = (Texture2D){ 0 };
  }

  if (terr->rock_texture.id > 0) {
    R3D_UnloadTexture(terr->rock_texture);
    terr->rock_texture = (Texture2D){ 0 };
  }

}

