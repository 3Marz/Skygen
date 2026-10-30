
#include "terrain.h"
#include "map_gen.h"
#include <raylib.h>


void generate_heightmap(Terrain *terr, MapGraph *map) {

}

void terrain_init(Terrain *terr) {

  terr->height_map = (Texture){ 0 };
  terr->model = (Model){ 0 };

}

void tmesh_push_tri();
void tmesh_set_vert(Mesh *tm, int i, float x, float y, float z) {
  tm->vertices[i*3    ] = x;
  tm->vertices[i*3 + 1] = y;
  tm->vertices[i*3 + 2] = z;
}
void tmesh_set_texcord(Mesh *tm, int i, float x, float y) {
  tm->texcoords[i*2    ] = x;
  tm->texcoords[i*2 + 1] = y;
}

void terrain_generate(Terrain *terr, MapGraph *map) {
  
  Mesh tm = { 0 };
  tm.triangleCount = 2;
  tm.vertexCount = tm.triangleCount * 3;

  tm.vertices = (float *)MemAlloc(tm.vertexCount * 3 * sizeof(float));
  tm.normals = (float *)MemAlloc(tm.vertexCount * 3 * sizeof(float));
  tm.texcoords = (float *)MemAlloc(tm.vertexCount * 2 * sizeof(float));

  tmesh_set_vert(&tm, 0, 0.0f, 0.0f, 0.0f);
  tmesh_set_vert(&tm, 1, 0.0f, 0.0f, 1.0f);
  tmesh_set_vert(&tm, 2, 1.0f, 0.0f, 1.0f);

  tmesh_set_vert(&tm, 3, 0.0f, 0.0f, 0.0f);
  tmesh_set_vert(&tm, 4, 1.0f, 0.0f, 1.0f);
  tmesh_set_vert(&tm, 5, 1.0f, 0.0f, 0.0f);

  for (int i = 0; i < tm.vertexCount; i++) {
    tm.normals[i*3  ] = 0.0f;
    tm.normals[i*3+1] = 1.0f;
    tm.normals[i*3+2] = 0.0f;
  }

  tmesh_set_texcord(&tm, 0, 0.0f, 0.0f);
  tmesh_set_texcord(&tm, 1, 0.0f, 0.0f);
  tmesh_set_texcord(&tm, 2, 0.0f, 0.0f);
  tmesh_set_texcord(&tm, 3, 0.0f, 0.0f);
  tmesh_set_texcord(&tm, 4, 0.0f, 0.0f);
  tmesh_set_texcord(&tm, 5, 0.0f, 0.0f);

  UploadMesh(&tm, false);

  terr->model = LoadModelFromMesh(tm);

}

void terrain_draw(Terrain *terr) {
  DrawGrid(10, 10);
  DrawModelWires(terr->model, (Vector3){0.0f, 0.0f, 0.0f}, 1.0f, WHITE);
}

void terrain_free(Terrain *terr) {

  UnloadModel(terr->model);
  UnloadTexture(terr->height_map);

}

