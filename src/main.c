#include "map_gen.h"
#include <raylib.h>

int main(void) {
  const int screenWidth = 1200;
  const int screenHeight = 800;

  // SetConfigFlags(FLAG_WINDOW_RESIZABLE);
  InitWindow(screenWidth, screenHeight, "SKYGEN");
  SetTargetFPS(60);

  MapParams params = {
    .map_size = (Vector2){1200, 800},
    .n_sites = 10000,
    .relax = 1,
  };

  // Voronoi Map
  MapGraph *v_map = map_graph_init(params);
  map_graph_generate(v_map, params);

  // Map Texture
  Texture2D mapTexture = map_graph_generate_texture(v_map);

  int regenerate = 0;

  while (!WindowShouldClose()) {

    // if (IsKeyPressed(KEY_Z)) { switchCamera = true; }
    if (IsKeyPressed(KEY_H)) { regenerate = 2; }
    if (IsKeyPressed(KEY_SPACE)) { regenerate = 1; }
    if (IsKeyPressed(KEY_UP)) { params.n_sites += 500; regenerate = 1; }
    if (IsKeyPressed(KEY_DOWN)) { params.n_sites -= 500; regenerate = 1; }

    if (regenerate) {
      if (regenerate == 1) {
        map_graph_generate(v_map, params);
      } else if (regenerate == 2) {
        map_graph_generate_heightfield(v_map);
      }
      mapTexture = map_graph_generate_texture(v_map);
      regenerate = 0;
    }

    // Draw
    BeginDrawing();
    ClearBackground(RAYWHITE);
      
      map_graph_draw(v_map, 0);
      
    EndDrawing();
  }

  UnloadTexture(mapTexture);

  map_graph_free(v_map);

  CloseWindow();

  return 0;
}
