
#include "generation_ctx.h"
#include "map_gen.h"
#include "terrain.h"
#include <raylib.h>

void regenerate_map(GenerationCtx *ctx) {
  if (ctx->regenerate == 1) {
    map_graph_generate(ctx->map, ctx->map_params);
  } else if (ctx->regenerate == 2) {
    map_graph_generate_heightfield(ctx->map);
  }
  ctx->regenerate = 0;
}

void gen_ctx_init(GenerationCtx *ctx) {

  ctx->screen_width = 1200;
  ctx->screen_height = 800;

  // SetConfigFlags(FLAG_WINDOW_RESIZABLE);
  InitWindow(ctx->screen_width, ctx->screen_height, "SKYGEN");
  SetTargetFPS(60);

  // Camera
  ctx->camera = (Camera){
    .fovy = 45.0f,
    .position = (Vector3){18.0f, 21.0f, 18.0f},
    .target = (Vector3){0.0f, 0.0f, 0.0f},
    .up = (Vector3){0.0f, 1.0f, 0.0f},
    .projection = CAMERA_PERSPECTIVE,
  };

  ctx->map_params = (MapParams){
    .map_size = (Vector2){1200, 800},
    .n_sites = 10000,
    .relax = 1,
  };

  // Voronoi Map
  MapGraph *v_map = map_graph_init(ctx->map_params);
  map_graph_generate(v_map, ctx->map_params);
  ctx->map = v_map;

  ctx->terrain = (Terrain){ 0 };
  terrain_init(&ctx->terrain);
  terrain_generate(&ctx->terrain, ctx->map);

  ctx->regenerate = 0;
  ctx->draw_mode = MODE_3D;

  DisableCursor();

}

void gen_ctx_update(GenerationCtx *ctx) {

  if (IsKeyPressed(KEY_H)) { ctx->regenerate = 2; }
  if (IsKeyPressed(KEY_M)) { ctx->draw_mode = ctx->draw_mode == MODE_2D ? MODE_3D : MODE_2D; }
  if (IsKeyPressed(KEY_SPACE)) { ctx->regenerate = 1; }
  if (IsKeyPressed(KEY_UP)) { ctx->map_params.n_sites += 500; ctx->regenerate = 1; }
  if (IsKeyPressed(KEY_DOWN)) { ctx->map_params.n_sites -= 500; ctx->regenerate = 1; }

  if (ctx->regenerate) {
    regenerate_map(ctx);
  }

  if (ctx->draw_mode == MODE_3D) {
    // DisableCursor();
    UpdateCamera(&ctx->camera, CAMERA_FREE);
  } else {
    // ShowCursor();
  }

}

void gen_ctx_draw(GenerationCtx *ctx) {

  ClearBackground(BLACK);
    
  switch (ctx->draw_mode) {
    case MODE_3D:
      BeginMode3D(ctx->camera);
        terrain_draw(&ctx->terrain);
      EndMode3D();
      break;
    case MODE_2D:
      map_graph_draw(ctx->map, 0, MODE_2D);
      break;
  }
}

void gen_ctx_free(GenerationCtx *ctx) {

  ctx->camera = (Camera){ 0 };
  ctx->map_params = (MapParams){ 0 };
  map_graph_free(ctx->map);
  terrain_free(&ctx->terrain);

}

