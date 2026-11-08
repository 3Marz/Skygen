
#include "generation_ctx.h"
#include "lights.h"
#include "map_gen.h"
#include "map_gen_height.h"
#include "terrain.h"
#include <raylib.h>
#include <raymath.h>
#include <limits.h>

void regenerate_map(GenerationCtx *ctx) {
  if (ctx->regenerate == 1) {
    map_graph_generate(ctx->map, ctx->map_params);
  } else if (ctx->regenerate == 2) {
    map_graph_generate_heightfield(ctx->map);
  } else if (ctx->regenerate == 3) {
    terrain_generate(&ctx->terrain, ctx->map, ctx->rand_seed, ctx->camera.position);
  } else if (ctx->regenerate == 4) {
    terrain_generate(&ctx->terrain, ctx->map, ctx->rand_seed, ctx->camera.position);
  }

  if (ctx->draw_mode == MODE_3D && (ctx->regenerate != 3 || ctx->regenerate != 4)) {
    terrain_generate(&ctx->terrain, ctx->map, ctx->rand_seed, ctx->camera.position);
  }


  ctx->regenerate = 0;
}

void export_height_map(GenerationCtx *ctx) {

  Image img = GenImageColor(ctx->map->texture_size.x+1, ctx->map->texture_size.y+1, BLACK);
  for (int i = 0; i < ctx->map->texture_size.x+1; i++) {
    for (int j = 0; j < ctx->map->texture_size.y+1; j++) {

      float h = map_heights_evaluate(ctx->map, (Vector2){i, j});
      h *= 225;
      Color hc = {h, h, h, 225};

      ImageDrawPixel(&img, i, j, hc);
    }
  }
  ImageBlurGaussian(&img, 2);
  // ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R16);
  ExportImage(img, TextFormat("public/height_seed_%i.png", ctx->rand_seed));
  UnloadImage(img);
}

void gen_ctx_init(GenerationCtx *ctx) {

  ctx->screen_width = 1200;
  ctx->screen_height = 800;

  SetConfigFlags(FLAG_WINDOW_RESIZABLE);
  InitWindow(ctx->screen_width, ctx->screen_height, "SKYGEN");
  SetTargetFPS(60);

  ctx->rand_seed = GetRandomValue(-UINT_MAX, UINT_MAX);
  // ctx->rand_seed = 666;
  SetRandomSeed(ctx->rand_seed);

  // Camera
  ctx->camera = (Camera){
    .fovy = 45.0f,
    .position = v3(1000.0f, 400.0f, 1000.0f),
    .target = v3(0.0f, 0.0f, 0.0f),
    .up = v3(0.0f, 1.0f, 0.0f),
    .projection = CAMERA_PERSPECTIVE,
  };

  ctx->camera_speed = 200.0f;

  ctx->map_params = (MapParams){
    .map_size = v2(MAP_SIZE_X, MAP_SIZE_Y),
    .n_sites = 30000,
    .relax = 1,
  };

  ctx->regenerate = 0;
  ctx->draw_mode = MODE_2D;

  // Lights

  ctx->lctx = (LightCtx){ 0 };
  light_ctx_init(&ctx->lctx);

  // Voronoi Map
  MapGraph *v_map = map_graph_init(ctx->map_params);
  map_graph_generate(v_map, ctx->map_params);
  ctx->map = v_map;

  ctx->terrain = (Terrain){ 0 };
  terrain_init(&ctx->terrain, &ctx->lctx.shader);

  if (ctx->draw_mode == MODE_3D) 
    terrain_generate(&ctx->terrain, ctx->map, ctx->rand_seed, ctx->camera.position);


  // DisableCursor();

}

static void update_free_camera(GenerationCtx *ctx) {
  if (IsKeyPressed(KEY_LEFT_BRACKET)) ctx->camera_speed *= 0.8f;
  if (IsKeyPressed(KEY_RIGHT_BRACKET)) ctx->camera_speed *= 1.25f;
  if (ctx->camera_speed < 10.0f) ctx->camera_speed = 10.0f;
  if (ctx->camera_speed > 10000.0f) ctx->camera_speed = 10000.0f;

  float step = ctx->camera_speed * GetFrameTime();

  // UpdateCameraPro movement: x = forward/back, y = right/left, z = up/down.
  Vector3 movement = { 0 };
  if (IsKeyDown(KEY_W)) movement.x += step;
  if (IsKeyDown(KEY_S)) movement.x -= step;
  if (IsKeyDown(KEY_D)) movement.y += step;
  if (IsKeyDown(KEY_A)) movement.y -= step;
  if (IsKeyDown(KEY_E)) movement.z += step;
  if (IsKeyDown(KEY_Q)) movement.z -= step;

  // Mouse look. UpdateCameraPro expects degrees and applies the sign itself,
  // so convert the stock rad-per-pixel sensitivity (0.003) to degrees.
  Vector2 delta = GetMouseDelta();
  Vector3 rotation = { delta.x * 0.003f * RAD2DEG,
                       delta.y * 0.003f * RAD2DEG,
                       0.0f };

  UpdateCameraPro(&ctx->camera, movement, rotation, 0.0f);
}

void gen_ctx_update(GenerationCtx *ctx) {

  if (IsKeyPressed(KEY_H)) { ctx->regenerate = 2; }
  if (IsKeyPressed(KEY_M)) { 
    ctx->draw_mode = ctx->draw_mode == MODE_2D ? MODE_3D : MODE_2D; 
    if (ctx->draw_mode == MODE_3D) DisableCursor(); else EnableCursor();
  }
  if (IsKeyPressed(KEY_N)) { ctx->regenerate = 3; }
  if (IsKeyPressed(KEY_SPACE)) { ctx->regenerate = 1; }
  if (IsKeyPressed(KEY_UP)) { ctx->map_params.n_sites += 500; ctx->regenerate = 1; }
  if (IsKeyPressed(KEY_DOWN)) { ctx->map_params.n_sites -= 500; ctx->regenerate = 1; }
  // if (IsKeyPressed(KEY_LEFT)) { ctx->terrain.grid.height_power += 5; ctx->regenerate = 4; }
  // if (IsKeyPressed(KEY_RIGHT)) { ctx->terrain.grid.height_power -= 5; ctx->regenerate = 4; }

  if (IsKeyPressed(KEY_P)) { export_height_map(ctx); }

  if (ctx->regenerate) {
    regenerate_map(ctx);
  }

  if (ctx->draw_mode == MODE_3D) {
    update_free_camera(ctx);
    light_ctx_update(&ctx->lctx, ctx->camera.position);
    terrain_update(&ctx->terrain, ctx->map, ctx->camera.position);
  } 

}

void gen_ctx_draw(GenerationCtx *ctx) {

  ClearBackground(BLACK);
    
  switch (ctx->draw_mode) {
    case MODE_3D:
      BeginMode3D(ctx->camera);
        BeginShaderMode(ctx->lctx.shader);
          terrain_draw(&ctx->terrain);
        EndShaderMode();
      EndMode3D();
      DrawText(TextFormat("camera speed: %.0f  ( [ / ] )", ctx->camera_speed), 10, 10, 20, RAYWHITE);
      break;
    case MODE_2D:
      map_graph_draw(ctx->map, 0);
      break;
  }
}

void gen_ctx_free(GenerationCtx *ctx) {

  ctx->camera = (Camera){ 0 };
  ctx->map_params = (MapParams){ 0 };
  map_graph_free(ctx->map);
  terrain_free(&ctx->terrain);
  light_ctx_free(&ctx->lctx);

}

