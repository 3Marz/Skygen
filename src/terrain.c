
#include "terrain.h"
#include "generation_ctx.h"
#include "height_field.h"
#include "map_gen.h"
#include "utils.h"
#include "../include/hashmap.h"
#include <raylib.h>
#include <rlgl.h>
#include <raymath.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RLIGHTS_IMPLEMENTATION
#include "../include/rlights.h"


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
  chunk->is_active = false;
  chunk->lod_level = -1;

  int nl[4] = {-1, -1, -1, -1};
  memcpy(&chunk->neigh_lods, &nl, sizeof(nl));
}

void tchunk_free(TChunk *chunk) {
  if (IsModelValid(chunk->model))
    UnloadModel(chunk->model);
  tgrid_free(&chunk->grid);
}

void tchunk_generate(TChunk *chunk, Terrain *terr, int lod_level) {

  // if (lod_level == chunk->lod_level && !IsModelValid(chunk->model)) return;
  // if (lod_level == chunk->lod_level) return;

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

  /* Normals come from the shared height field, so a vertex on a chunk border
   * gets the same normal from both chunks no matter which LOD built it. */
  Vector3 *normals = (Vector3 *)malloc((size_t)width * height * sizeof(Vector3));

  for (int x = 0; x < width; x++) {
    for (int y = 0; y < height; y++) {

      int i = x * height + y; 
      Vector3 point = chunk->grid.vert[x][y];

      Vector3 normal = normals
        ? height_field_normal(
            &terr->height_field,
            chunk->position.x + point.x,
            chunk->position.z + point.z,
            chunk->grid.height_power)
        : (Vector3){ 0, 1, 0 };
      if (normals) normals[i] = normal;

      tmesh_set_vert(&tm, i, point);
      tmesh_set_normal(&tm, i, normal);

      tmesh_set_texcord(&tm, i, 
        (float)x / (width - 1),
        (float)y / (height - 1)
      );

    }
  }

  /* On a stitched side the finer chunk pulls its odd border vertices onto the
   * midpoint of the coarse edge. Do the same for their normals, so both sides
   * interpolate the exact same normal along the shared edge. */
  if (normals) {
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

        Vector3 avg = Vector3Normalize(Vector3Add(n0, n1));
        normals[x * height + y] = avg;
        tmesh_set_normal(&tm, x * height + y, avg);
      }
    }

    free(normals);
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

    }
  }

  UploadMesh(&tm, false);

  chunk->model = LoadModelFromMesh(tm);
  chunk->is_active = true;
}

TChunk* get_chunk(Terrain *terr, int x, int y) {
  if ((x >= 0 && x < terr->nchunk.x) && (y >= 0 && y < terr->nchunk.y))
    return &terr->chunk[x][y];

  return NULL;
}

Material LoadTerrainMaterial(MaterialLoc *locs) {
  Material mat = { 0 };

  const int MAX_MATERIAL_MAPS = 12;

  mat.maps = (MaterialMap *)RL_CALLOC(MAX_MATERIAL_MAPS, sizeof(MaterialMap));

  // Using PBR Shader 
  mat.shader = LoadShader("assets/shaders/pbr.vs", "assets/shaders/pbr.fs");
  mat.shader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(mat.shader, "viewPos");
  mat.shader.locs[SHADER_LOC_MAP_ALBEDO] = GetShaderLocation(mat.shader, "albedoMap");
  // WARNING: Metalness, roughness, and ambient occlusion are all packed into a
  // MRA texture They are passed as to the SHADER_LOC_MAP_METALNESS location for
  // convenience, mat.shader already takes care of it accordingly
  mat.shader.locs[SHADER_LOC_MAP_METALNESS] = GetShaderLocation(mat.shader, "mraMap");
  mat.shader.locs[SHADER_LOC_MAP_NORMAL] = GetShaderLocation(mat.shader, "normalMap");
  // WARNING: Similar to the MRA map, the emissive map packs different
  // information into a single texture: it stores height and emission data It is
  // binded to SHADER_LOC_MAP_EMISSION location an properly processed on mat.shader
  mat.shader.locs[SHADER_LOC_MAP_EMISSION] = GetShaderLocation(mat.shader, "emissiveMap");
  mat.shader.locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(mat.shader, "albedoColor");

  int lightCountLoc = GetShaderLocation(mat.shader, "numOfLights");
  int maxLightCount = MAX_LIGHTS;
  SetShaderValue(mat.shader, lightCountLoc, &maxLightCount, SHADER_UNIFORM_INT);

  // Ambient light level (some basic lighting)
  float ambientIntensity = 0.02f;
  Color ambientColor = WHITE;
  float ambientColorNormalized[4] = { ambientColor.r/255.0f, ambientColor.g/255.0f, ambientColor.b/255.0f, ambientColor.a/255.0f };
  SetShaderValue( mat.shader, GetShaderLocation(mat.shader, "ambientColor"), &ambientColorNormalized, SHADER_UNIFORM_VEC4);
  SetShaderValue( mat.shader, GetShaderLocation(mat.shader, "ambient"), &ambientIntensity, SHADER_UNIFORM_VEC4);

  locs->metallic_value_loc = GetShaderLocation(mat.shader, "metallicValue");
  locs->roughness_value_loc = GetShaderLocation(mat.shader, "roughnessValue");
  locs->emissive_intensity_loc = GetShaderLocation(mat.shader, "emissivePower");
  locs->emissive_color_loc = GetShaderLocation(mat.shader, "emissiveColor");
  locs->texture_tiling_loc = GetShaderLocation(mat.shader, "tiling");

  // // Using rlgl default texture (1x1 pixel, UNCOMPRESSED_R8G8B8A8, 1 mipmap)
  // mat.maps[MATERIAL_MAP_DIFFUSE].texture = (Texture2D){ rlGetTextureIdDefault(), 1, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
  // //mat.maps[MATERIAL_MAP_NORMAL].texture;         // NOTE: By default, not set
  // //mat.maps[MATERIAL_MAP_SPECULAR].texture;       // NOTE: By default, not set
  //
  // mat.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;    // Diffuse color
  // mat.maps[MATERIAL_MAP_SPECULAR].color = WHITE;   // Specular color

  mat.maps[MATERIAL_MAP_ALBEDO].color = WHITE;
  mat.maps[MATERIAL_MAP_METALNESS].value = 0.8f;
  mat.maps[MATERIAL_MAP_ROUGHNESS].value = 0.1f;
  mat.maps[MATERIAL_MAP_OCCLUSION].value = 1.0f;
  mat.maps[MATERIAL_MAP_EMISSION].color = WHITE;

  // mat.maps default textures
  mat.maps[MATERIAL_MAP_ALBEDO].texture = LoadTexture("assets/textures/Grass/Grass005_1K-PNG_Color.png");
  mat.maps[MATERIAL_MAP_METALNESS].texture = LoadTexture("assets/textures/Grass/Grass005_1K-PNG_Roughness.png");
  mat.maps[MATERIAL_MAP_NORMAL].texture = LoadTexture("assets/textures/Grass/Grass005_1K-PNG_NormalGL.png");
  mat.maps[MATERIAL_MAP_EMISSION].texture = LoadTexture("assets/textures/Grass/Grass005_1K-PNG_Displacement.png");

  
  int usage = 1;
  SetShaderValue(mat.shader, GetShaderLocation(mat.shader, "useTexAlbedo"), &usage, SHADER_UNIFORM_INT);
  SetShaderValue(mat.shader, GetShaderLocation(mat.shader, "useTexNormal"), &usage, SHADER_UNIFORM_INT);
  SetShaderValue(mat.shader, GetShaderLocation(mat.shader, "useTexMRA"), &usage, SHADER_UNIFORM_INT);
  SetShaderValue(mat.shader, GetShaderLocation(mat.shader, "useTexEmissive"), &usage, SHADER_UNIFORM_INT);

  return mat;
}

void terrain_init(Terrain *terr) {

  terr->wire_draw = true;
  terr->locs = (MaterialLoc){ 0 };
  terr->material = LoadTerrainMaterial(&terr->locs);
  terr->dir_light = CreateLight(LIGHT_DIRECTIONAL, v3(1.f, 1.f, 1.f), Vector3Zero(), WHITE, 5.0f, terr->material.shader);
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

  // Update material shader
  int cp[3] = {cam_pos.x, cam_pos.y, cam_pos.z};
  SetShaderValue(terr->material.shader, terr->material.shader.locs[SHADER_LOC_VECTOR_VIEW], cp, SHADER_UNIFORM_VEC3);
  UpdateLightValues(terr->material.shader, terr->dir_light);

  if (IsKeyPressed(KEY_COMMA)) {
    terr->wire_draw = terr->wire_draw ? false : true;
  }

  if (!terr->height_field.data)
    height_field_build(&terr->height_field, map);

  Vector2 curr_chunk = get_chunk_idx_v(terr, cam_pos);

  if (!Vector2Equals(curr_chunk, terr->last_chunk)) {
    update_chunks(terr, curr_chunk);
    terr->last_chunk = curr_chunk;
  }

}

void terrain_draw(Terrain *terr) {

  Shader shader = terr->material.shader;
  Vector2 ground_texture_tiling = v2(0.5f, 0.5f);

  // Set old car model texture tiling, emissive color and emissive intensity parameters on shader
  SetShaderValue(shader, terr->locs.texture_tiling_loc, &ground_texture_tiling, SHADER_UNIFORM_VEC2);
  Vector4 emissive_color = ColorNormalize(terr->material.maps[MATERIAL_MAP_EMISSION].color);
  SetShaderValue(shader, terr->locs.emissive_color_loc, &emissive_color, SHADER_UNIFORM_VEC4);
  float emissive_intensity = 0.01f;
  SetShaderValue(shader, terr->locs.emissive_intensity_loc, &emissive_intensity, SHADER_UNIFORM_FLOAT);

  // Set old car metallic and roughness values
  SetShaderValue(shader, terr->locs.metallic_value_loc, &terr->material.maps[MATERIAL_MAP_METALNESS].value, SHADER_UNIFORM_FLOAT);
  SetShaderValue(shader, terr->locs.roughness_value_loc, &terr->material.maps[MATERIAL_MAP_ROUGHNESS].value, SHADER_UNIFORM_FLOAT);

  for (int x = 0; x < terr->nchunk.x; x++) {
    for (int y = 0; y < terr->nchunk.y; y++) {

      TChunk *c = get_chunk(terr, x, y);
      if (c->is_active && IsModelValid(c->model)) {
        if (terr->wire_draw) {
          DrawModelWires(c->model, c->position, 1.0f, WHITE);
        } else {
          DrawMesh(c->model.meshes[0], terr->material, MatrixTranslate(c->position.x, c->position.y, c->position.z));
        }
      }
    }
  }

}

void terrain_free(Terrain *terr) {

  UnloadMaterial(terr->material);
  terr->material.maps = NULL;

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

}

