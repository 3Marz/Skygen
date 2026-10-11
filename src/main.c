// #include "open-simplex-noise.h"
#include "map_gen.c"
#include <math.h>
#include <raylib.h>
#include <stdio.h>

int main(void) {
  const int screenWidth = 1080;
  const int screenHeight = 720;

  InitWindow(screenWidth, screenHeight, "SKYGEN");

  Params params = {
    .map_size = (Vector2){1024, 1024},
    .n_sites = 1000,
    .relax = 2,
  };

  // Voronoi Map
  MapData *v_map = initVoronoiMap(params);
  generateVoronoiMap(v_map, params);

  // struct osn_context *noise_ctx;
  // open_simplex_noise(GetRandomValue(0, 10000), &noise_ctx);

  // Camera setup
  // Camera camera = {0};
  // camera.target = (Vector3){0.0f, 5.0f, 0.0f};
  // camera.position = (Vector3){50.0f, 40.0f, 50.0f};
  // camera.up = (Vector3){0.0f, 1.0f, 0.0f};
  // camera.fovy = 45.0f;
  // camera.projection = CAMERA_PERSPECTIVE;
  //
  // int cameraMode = CAMERA_ORBITAL;
  //
  // bool update = true;
  // bool switchCamera = false;
  // bool isFP = false;

  // Map Texture
  Texture2D mapTexture = genVoronoiTexture(v_map);

  // Perlin noise
  // Texture2D noiseTexture = LoadTextureFromImage(v_map->noise_img);

  // Mesh terrainMesh = GenMeshHeightmap(noiseImage,
  // (Vector3){ 50.0f, 30.0f, 50.0f }); Model terrainModel =
  // LoadModelFromMesh(terrainMesh);

  // terrainModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture =
  // noiseTexture;
  

  // for (int i = 0; i < v_map->nc; i++) {
  //   printf("center %d = (%f, %f) corners: %d\n", i, v_map->c[i].pos.x,
  //          v_map->c[i].pos.y, v_map->c[i].ncorners);
  // }
  
  SetTargetFPS(60);

  int regenerate = 0;

  while (!WindowShouldClose()) {
    // Update
    // UpdateCamera(&camera, cameraMode);

    // if (IsKeyPressed(KEY_Z)) { switchCamera = true; }
    // if (IsKeyPressed(KEY_R)) { relaxVoronoiMap(v_map, 1); regenerate = 1; }
    if (IsKeyPressed(KEY_SPACE)) { 
      regenerate = 1;
    }
    if (IsKeyPressed(KEY_UP)) {
      params.n_sites += 200;
      regenerate = 1;
    }
    if (IsKeyPressed(KEY_DOWN)) {
      params.n_sites -= 200;
      regenerate = 1;
    }

    if (regenerate) {
      generateVoronoiMap(v_map, params);

      UnloadTexture(mapTexture);
      mapTexture = genVoronoiTexture(v_map);

      regenerate = 0;
    }

    // if (switchCamera) {
    //   if (isFP) {
    //     cameraMode = CAMERA_ORBITAL;
    //     EnableCursor();
    //     isFP = false;
    //   } else {
    //     cameraMode = CAMERA_FIRST_PERSON;
    //     DisableCursor();
    //     isFP = true;
    //   }
    //   camera.target = (Vector3){0.0f, 5.0f, 0.0f};
    //   camera.position = (Vector3){40.0f, 30.0f, 40.0f};
    //   switchCamera = false;
    // }
    //
    // camera.position.y = 30.0f;

    // Draw
    BeginDrawing();
    ClearBackground(BLACK);
      
      // BeginMode3D(camera);
      //     // DrawGrid(25, 5.0f);
      //     DrawModelWires(terrainModel, (Vector3){ -25.0f, -15.0f, -25.0f
      //     }, 1.0f, WHITE);
      // EndMode3D();

      DrawTextureEx(mapTexture, (Vector2){0.0f, 0.0f}, 0.0f, 0.7f, WHITE);
      // DrawTextureEx(noiseTexture, (Vector2){750.0f, 0.0f}, 0.0f, 0.3f, WHITE);
      

      // DrawFPS(20, 20);
      // for (int i = 0; i < v_map->nc; i++) {
      //   DrawCircleV(v_map->c[i].pos, 3.0f, YELLOW);
      // }

    EndDrawing();
  }

  UnloadTexture(mapTexture);

  freeVoronoiMap(v_map);

  CloseWindow();

  return 0;
}
