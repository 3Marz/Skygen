
#include "terrain.h"
#include "generation_ctx.h"
#include "map_gen.h"
#include "map_gen_height.h"
#include <raylib.h>
#include <raymath.h>
#include <stdlib.h>


void tgrid_init(TGrid *grid, Vector2 size, float spacing) {

  grid->height_power = spacing*2;
  grid->size = size;
  grid->spacing = spacing;

  grid->vert = (Vector3 **)malloc(grid->size.x * sizeof(Vector3 *));
  for (int i = 0; i < grid->size.x; i++) {
    grid->vert[i] = (Vector3 *)malloc(grid->size.y * sizeof(Vector3));
  }

}

void tgrid_generate(TGrid *grid, Vector2 offset, MapGraph *map) {
  if (!grid->vert) return;

  for (int x = 0; x < grid->size.x; x++) {
    for (int y = 0; y < grid->size.y; y++) {

      int px = x * grid->spacing; 
      int pz = y * grid->spacing; 

      // printf("x: %f, y: %f\n", x+offset.x, y+offset.y);
      // Color hc = GetImageColor(height_map, x+offset.x, y+offset.y);
      // float h = Normalize((float)(hc.r+hc.g+hc.b)/3, 0, 255);
      float h = map_heights_evaluate(map, v2(x+offset.x, y+offset.y));

      grid->vert[x][y] = (Vector3){px, h*grid->height_power, pz};
    }
  }

}

void tgrid_free(TGrid *grid) {
  for (int i = 0; i < grid->size.x; i++) {
    free(grid->vert[i]);
    grid->vert[i] = NULL;
  }
  free(grid->vert);
  grid->vert = NULL;
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
}

void tchunk_free(TChunk *chunk) {
  if (IsModelValid(chunk->model))
    UnloadModel(chunk->model);
  tgrid_free(&chunk->grid);
}

void tchunk_generate(TChunk *chunk, MapGraph *map, Shader *light_shader) {
  tchunk_free(chunk);

  float s = 256.0f;
  tgrid_init(&chunk->grid, (Vector2){s+1, s+1}, CHUNCK_SPACE/(s));
  tgrid_generate(&chunk->grid, 
      (Vector2){chunk->position.x, chunk->position.z},
      map);

  Mesh tm = {0};
  tm.triangleCount = 2 * ((chunk->grid.size.x - 1) * (chunk->grid.size.y - 1));
  if (tm.triangleCount <= 0)
    return;
  tm.vertexCount = tm.triangleCount * 3;

  tm.vertices = (float *)MemAlloc(tm.vertexCount * 3 * sizeof(float));
  tm.normals = (float *)MemAlloc(tm.vertexCount * 3 * sizeof(float));
  tm.texcoords = (float *)MemAlloc(tm.vertexCount * 2 * sizeof(float));

  int vi = 0;
  for (int x = 0; x < chunk->grid.size.x - 1; x++) {
    for (int y = 0; y < chunk->grid.size.y - 1; y++) {

      //-------------------------First Tri in face---------------------------//
      Vector3 a1 = chunk->grid.vert[0 + x][0 + y],
              b1 = chunk->grid.vert[0 + x][1 + y],
              c1 = chunk->grid.vert[1 + x][0 + y];

      tmesh_set_vert(&tm, vi,     a1);
      tmesh_set_vert(&tm, vi + 1, b1);
      tmesh_set_vert(&tm, vi + 2, c1);
      Vector3 edgeA1 = Vector3Subtract(b1, a1);
      Vector3 edgeB1 = Vector3Subtract(c1, a1);
      Vector3 face_normal1 = Vector3CrossProduct(edgeA1, edgeB1);

      tmesh_add_normal(&tm, vi,   face_normal1);
      tmesh_add_normal(&tm, vi+1, face_normal1);
      tmesh_add_normal(&tm, vi+2, face_normal1);

      //-------------------------Second Tri in face---------------------------//
      Vector3 a2 = chunk->grid.vert[0 + x][1 + y],
              b2 = chunk->grid.vert[1 + x][1 + y],
              c2 = chunk->grid.vert[1 + x][0 + y];

      tmesh_set_vert(&tm, vi + 3, a2);
      tmesh_set_vert(&tm, vi + 4, b2);
      tmesh_set_vert(&tm, vi + 5, c2);
      Vector3 edgeA2 = Vector3Subtract(b1, a1);
      Vector3 edgeB2 = Vector3Subtract(c1, a1);
      Vector3 face_normal2 = Vector3CrossProduct(edgeA2, edgeB2);

      tmesh_add_normal(&tm, vi+3, face_normal2);
      tmesh_add_normal(&tm, vi+4, face_normal2);
      tmesh_add_normal(&tm, vi+5, face_normal2);

      vi += 6;
    }
  }

  for (int i = 0; i < tm.vertexCount; i++) {
    Vector3 v_normal = v3(tm.normals[i * 3], tm.normals[i * 3+1], tm.normals[i * 3+2]); 
    tmesh_set_normal(&tm, i, Vector3Normalize(v_normal));
    // tm.normals[i * 3] = 0.0f;
    // tm.normals[i * 3 + 1] = 1.0f;
    // tm.normals[i * 3 + 2] = 0.0f;

    // tm.texcoords[i * 2] = 0.0f;
    // tm.texcoords[i * 2 + 1] = 0.0f;
    tmesh_set_texcord(&tm, i  , 0.0f, 0.0f);
  }

  UploadMesh(&tm, false);

  chunk->model = LoadModelFromMesh(tm);
  chunk->model.materials[0].shader = *light_shader;
}

void terrain_init(Terrain *terr, Shader *light_shader) {

  terr->light_shader = light_shader;
  terr->nchunk = (Vector2){
    (float)MAP_SIZE_X/CHUNCK_SPACE, (float)MAP_SIZE_Y/CHUNCK_SPACE
  };

  terr->chunk = (TChunk **)malloc(terr->nchunk.x * sizeof(TChunk *));
  for (int i = 0; i < terr->nchunk.y; i++) {
    terr->chunk[i] = (TChunk *)malloc(terr->nchunk.y * sizeof(TChunk));
  }

  for (int x = 0; x < terr->nchunk.x; x++) {
    for (int y = 0; y < terr->nchunk.y; y++) {
      tchunk_init(&terr->chunk[x][y], (Vector3){x*CHUNCK_SPACE, 0, y*CHUNCK_SPACE});
    }
  }

}

void terrain_generate(Terrain *terr, MapGraph *map, int seed, Vector3 inital_cam_pos) {

  terr->height_map = LoadImage(TextFormat("height_seed_%i.png", seed));

  for (int x = 0; x < terr->nchunk.x; x++) {
    for (int y = 0; y < terr->nchunk.y; y++) {

      TChunk *c = &terr->chunk[x][y];
      Vector2 ch_pos2 = {c->position.x, c->position.z};
      Vector2 cm_pos2 = {inital_cam_pos.x, inital_cam_pos.z};
      float dist = Vector2DistanceSqr(ch_pos2, cm_pos2);

      if (dist > VIEW_DIST) continue;

      tchunk_generate(c, map, terr->light_shader);
      c->is_active = true;
    }
  }
}

void terrain_update(Terrain *terr, MapGraph *map, Vector3 cam_pos) {
  for (int x = 0; x < terr->nchunk.x; x++) {
    for (int y = 0; y < terr->nchunk.y; y++) {
      TChunk *c = &terr->chunk[x][y];
      Vector2 ch_pos2 = {c->position.x, c->position.z};
      Vector2 cm_pos2 = {cam_pos.x, cam_pos.z};
      float dist = Vector2DistanceSqr(ch_pos2, cm_pos2);

      // if (dist > 100000.0f) {
      //   if (c->is_active) {
      //     c->is_active = false;
      //     tchunk_free(c);
      //   }
      // } else {
      //   if (!c->is_active) {
      //     c->is_active = true;
      //     tchunk_generate(c, terr->height_map);
      //   }
      // }
      
      if (dist > VIEW_DIST) {
        c->is_active = false;
      } else {
        if (!IsModelValid(c->model) && IsImageValid(terr->height_map))
          tchunk_generate(c, map, terr->light_shader);
        c->is_active = true;
      }

    }
  }
}

void terrain_draw(Terrain *terr) {

  // float sizex = (terr->grid.size.x*terr->grid.spacing)-terr->grid.spacing,
  //       sizey = (terr->grid.size.y*terr->grid.spacing)-terr->grid.spacing;
  //
  // DrawModelWires(terr->model, (Vector3){-(sizex/4), 0.0f, -(sizey/4)}, 1.0f, WHITE);
  for (int x = 0; x < terr->nchunk.x; x++) {
    for (int y = 0; y < terr->nchunk.y; y++) {

      TChunk *c = &terr->chunk[x][y];
      if (c->is_active && IsModelValid(c->model)) {
        DrawModel(c->model, c->position, 1.0f, (Color){61, 245, 73, 225});
      }
    }
  }

}

void terrain_free(Terrain *terr) {

  for (int x = 0; x < terr->nchunk.x; x++) {
    for (int y = 0; y < terr->nchunk.y; y++) {
      tchunk_free(&terr->chunk[x][y]);
    }
  }
  for (int x = 0; x < terr->nchunk.x; x++) {
    free(terr->chunk[x]); terr->chunk[x] = NULL;
  }
  free(terr->chunk); terr->chunk = NULL;

  UnloadImage(terr->height_map);

}

