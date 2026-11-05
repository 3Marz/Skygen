
#include "terrain.h"
#include "map_gen.h"
#include "map_gen_height.h"
#include <raylib.h>
#include <raymath.h>
#include <stdio.h>
#include <stdlib.h>


void tgrid_init(TGrid *grid, Vector2 size, float spacing) {

  grid->height_power = 50.0f;
  grid->size = size;
  grid->spacing = spacing;

  grid->vert = (Vector3 **)malloc(grid->size.x * sizeof(Vector3 *));
  for (int i = 0; i < grid->size.x; i++) {
    grid->vert[i] = (Vector3 *)malloc(grid->size.y * sizeof(Vector3));
  }

}

void tgrid_generate(TGrid *grid, MapGraph *map, int no_map_lookup) {
  if (!grid->vert) return;

  for (int x = 0; x < grid->size.x; x++) {
    for (int y = 0; y < grid->size.y; y++) {

      int px = x * grid->spacing; 
      int pz = y * grid->spacing; 

      float h;
      if (!no_map_lookup) {
        Vector2 normalized_pos = {
          Normalize(px, 0, grid->size.x * grid->spacing - grid->spacing) * map->texture_size.x,
          Normalize(pz, 0, grid->size.y * grid->spacing - grid->spacing) * map->texture_size.y
        };
        h = map_heights_evaluate(map, normalized_pos);
      } else {
        h = grid->vert[x][y].y;
      }

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

void terrain_init(Terrain *terr) {

  terr->height_map = (Texture){ 0 };
  terr->model = (Model){ 0 };
  terr->grid = (TGrid){ 0 };

}

void tmesh_push_tri();
void tmesh_set_vert(Mesh *tm, int i, Vector3 p) {
  tm->vertices[i*3    ] = p.x;
  tm->vertices[i*3 + 1] = p.y;
  tm->vertices[i*3 + 2] = p.z;
}
void tmesh_set_texcord(Mesh *tm, int i, float x, float y) {
  tm->texcoords[i*2    ] = x;
  tm->texcoords[i*2 + 1] = y;
}

void terrain_generate(Terrain *terr, MapGraph *map, int no_map_lookup) {

  terrain_free(terr);

  tgrid_init(&terr->grid, (Vector2){map->texture_size.x, map->texture_size.y}, map->spacing*2);
  tgrid_generate(&terr->grid, map, no_map_lookup);
  
  Mesh tm = { 0 };
  tm.triangleCount = 2 * ((terr->grid.size.x-1) * (terr->grid.size.y-1)) ;
  if (tm.triangleCount <= 0) return;
  tm.vertexCount = tm.triangleCount * 3;

  tm.vertices = (float *)MemAlloc(tm.vertexCount * 3 * sizeof(float));
  tm.normals = (float *)MemAlloc(tm.vertexCount * 3 * sizeof(float));
  tm.texcoords = (float *)MemAlloc(tm.vertexCount * 2 * sizeof(float));

  int vi = 0;
  for (int x = 0; x < terr->grid.size.x - 1; x++) {
    for (int y = 0; y < terr->grid.size.y - 1; y++) {
      tmesh_set_vert(&tm, vi  , terr->grid.vert[0+x][0+y]);
      tmesh_set_vert(&tm, vi+1, terr->grid.vert[0+x][1+y]);
      tmesh_set_vert(&tm, vi+2, terr->grid.vert[1+x][0+y]);

      tmesh_set_vert(&tm, vi+3, terr->grid.vert[0+x][1+y]);
      tmesh_set_vert(&tm, vi+4, terr->grid.vert[1+x][1+y]);
      tmesh_set_vert(&tm, vi+5, terr->grid.vert[1+x][0+y]);
      vi+=6;
    }
  }

  for (int i = 0; i < tm.vertexCount; i++) {
    tm.normals[i*3  ] = 0.0f;
    tm.normals[i*3+1] = 1.0f;
    tm.normals[i*3+2] = 0.0f;

    tm.texcoords[i*2    ] = 0.0f;
    tm.texcoords[i*2 + 1] = 0.0f;
    // tmesh_set_texcord(&tm, i*2  , 0.0f, 0.0f);
    // tmesh_set_texcord(&tm, i*2+1, 0.0f, 0.0f);
  }

  UploadMesh(&tm, false);

  terr->model = LoadModelFromMesh(tm);

}

void terrain_draw(Terrain *terr) {

  float sizex = (terr->grid.size.x*terr->grid.spacing)-terr->grid.spacing,
        sizey = (terr->grid.size.y*terr->grid.spacing)-terr->grid.spacing;

  DrawModelWires(terr->model, (Vector3){-(sizex/4), 0.0f, -(sizey/4)}, 1.0f, WHITE);

}

void terrain_free(Terrain *terr) {

  tgrid_free(&terr->grid);
  UnloadModel(terr->model);
  UnloadTexture(terr->height_map);

}

