// #include "open-simplex-noise.h"
#include "map_gen.h"
#include <math.h>
#include <raylib.h>
#include <stdio.h>

int main(void) {
  const int screenWidth = 1200;
  const int screenHeight = 800;

  // SetConfigFlags(FLAG_WINDOW_RESIZABLE);
  InitWindow(screenWidth, screenHeight, "SKYGEN");
  SetTargetFPS(60);

  Params params = {
    .map_size = (Vector2){1200, 800},
    .n_sites = 10000,
    .relax = 1,
  };

  // Voronoi Map
  MapData *v_map = init_voronoi_map(params);
  generate_voronoi_map(v_map, params);

  // Map Texture
  Texture2D mapTexture = gen_voronoi_texture(v_map);

  int regenerate = 0;

  while (!WindowShouldClose()) {

    // if (IsKeyPressed(KEY_Z)) { switchCamera = true; }
    if (IsKeyPressed(KEY_H)) { regenerate = 2; }
    if (IsKeyPressed(KEY_SPACE)) { regenerate = 1; }
    if (IsKeyPressed(KEY_UP)) { params.n_sites += 200; regenerate = 1; }
    if (IsKeyPressed(KEY_DOWN)) { params.n_sites -= 200; regenerate = 1; }

    if (regenerate) {
      if (regenerate == 1) {
        generate_voronoi_map(v_map, params);
      } else if (regenerate == 2) {
        generate_heightmap(v_map);
      }
      mapTexture = gen_voronoi_texture(v_map);
      regenerate = 0;
    }

    // Draw
    BeginDrawing();
    ClearBackground(RAYWHITE);
      
      draw_voronoi_map(v_map, 0);
      
    EndDrawing();
  }

  UnloadTexture(mapTexture);

  free_voronoi_map(v_map);

  CloseWindow();

  return 0;
}
