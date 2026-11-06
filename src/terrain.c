
#include "terrain.h"
#include "generation_ctx.h"
#include "map_gen.h"
#include "../include/sds.h"
#include <raylib.h>
#include <raymath.h>
#include <stdlib.h>


void tgrid_init(TGrid *grid, Vector2 size, float spacing) {

  grid->height_power = 200.0f;
  grid->size = size;
  grid->spacing = spacing;

  grid->vert = (Vector3 **)malloc(grid->size.x * sizeof(Vector3 *));
  for (int i = 0; i < grid->size.x; i++) {
    grid->vert[i] = (Vector3 *)malloc(grid->size.y * sizeof(Vector3));
  }

}

void tgrid_generate(TGrid *grid, Vector2 offset, Image height_map) {
  if (!grid->vert) return;

  for (int x = 0; x < grid->size.x; x++) {
    for (int y = 0; y < grid->size.y; y++) {

      int px = x * grid->spacing; 
      int pz = y * grid->spacing; 

      // printf("x: %f, y: %f\n", x+offset.x, y+offset.y);
      Color hc = GetImageColor(height_map, x+offset.x, y+offset.y);
      float h = Normalize((float)(hc.r+hc.g+hc.b)/3, 0, 255);

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

void tchunk_generate(TChunk *chunk, Image height_map) {
  tchunk_free(chunk);

  float s = 64.0f;
  tgrid_init(&chunk->grid, (Vector2){s+1, s+1}, CHUNCK_SPACE/(s));
  tgrid_generate(&chunk->grid, 
      (Vector2){chunk->position.x, chunk->position.z},
      height_map);

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
      tmesh_set_vert(&tm, vi, chunk->grid.vert[0 + x][0 + y]);
      tmesh_set_vert(&tm, vi + 1, chunk->grid.vert[0 + x][1 + y]);
      tmesh_set_vert(&tm, vi + 2, chunk->grid.vert[1 + x][0 + y]);

      tmesh_set_vert(&tm, vi + 3, chunk->grid.vert[0 + x][1 + y]);
      tmesh_set_vert(&tm, vi + 4, chunk->grid.vert[1 + x][1 + y]);
      tmesh_set_vert(&tm, vi + 5, chunk->grid.vert[1 + x][0 + y]);
      vi += 6;
    }
  }

  for (int i = 0; i < tm.vertexCount; i++) {
    tm.normals[i * 3] = 0.0f;
    tm.normals[i * 3 + 1] = 1.0f;
    tm.normals[i * 3 + 2] = 0.0f;

    tm.texcoords[i * 2] = 0.0f;
    tm.texcoords[i * 2 + 1] = 0.0f;
    // tmesh_set_texcord(&tm, i*2  , 0.0f, 0.0f);
    // tmesh_set_texcord(&tm, i*2+1, 0.0f, 0.0f);
  }

  UploadMesh(&tm, false);

  chunk->model = LoadModelFromMesh(tm);
}

void tchunk_free(TChunk *chunk) {
  UnloadModel(chunk->model);
  tgrid_free(&chunk->grid);
}

void terrain_init(Terrain *terr) {

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

void terrain_generate(Terrain *terr, MapGraph *map, int seed) {

  sds path = sdsnew("public/");
  path = sdscatprintf(path, "height_seed_%i.png", seed);
  terr->height_map = LoadImage(path);

  for (int x = 0; x < terr->nchunk.x; x++) {
    for (int y = 0; y < terr->nchunk.y; y++) {
      tchunk_generate(&terr->chunk[x][y], terr->height_map);
      terr->chunk[x][y].is_active = true;
    }
  }
}

void terrain_update(Terrain *terr, Vector3 cam_pos) {
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
      
      if (dist > 100000.0f) {
        c->is_active = false;
      } else {
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
      if (c->is_active && IsModelValid(c->model))
        DrawModelWires(c->model, c->position, 1.0f, WHITE);

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

