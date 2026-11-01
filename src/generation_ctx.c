
#include "generation_ctx.h"
#include "map_gen.h"
#include "terrain.h"
#include <raylib.h>
#include <raymath.h>

void regenerate_map(GenerationCtx *ctx) {
  if (ctx->regenerate == 1) {
    map_graph_generate(ctx->map, ctx->map_params);
  } else if (ctx->regenerate == 2) {
    map_graph_generate_heightfield(ctx->map);
  } else if (ctx->regenerate == 3) {
    terrain_generate(&ctx->terrain, ctx->map);
  }

  if (ctx->draw_mode == MODE_3D && ctx->regenerate != 3) {
    terrain_generate(&ctx->terrain, ctx->map);
  }

  ctx->regenerate = 0;
}

void gen_ctx_init(GenerationCtx *ctx) {

  ctx->screen_width = 1200;
  ctx->screen_height = 800;

  SetConfigFlags(FLAG_WINDOW_RESIZABLE);
  InitWindow(ctx->screen_width, ctx->screen_height, "SKYGEN");
  SetTargetFPS(60);

  // Camera
  ctx->camera = (Camera){
    .fovy = 45.0f,
    .position = (Vector3){1000.0f, 400.0f, 1000.0f},
    .target = (Vector3){0.0f, 0.0f, 0.0f},
    .up = (Vector3){0.0f, 1.0f, 0.0f},
    .projection = CAMERA_PERSPECTIVE,
  };

  // The terrain spans thousands of world units, so the stock raylib speed of
  // ~5 units/s is unusably slow. This one is changeable at runtime.
  ctx->camera_speed = 300.0f;

  ctx->map_params = (MapParams){
    .map_size = (Vector2){1200, 800},
    .n_sites = 10000,
    .relax = 1,
  };

  ctx->regenerate = 0;
  ctx->draw_mode = MODE_2D;

  // Voronoi Map
  MapGraph *v_map = map_graph_init(ctx->map_params);
  map_graph_generate(v_map, ctx->map_params);
  ctx->map = v_map;

  ctx->terrain = (Terrain){ 0 };
  terrain_init(&ctx->terrain);

  if (ctx->draw_mode == MODE_3D) 
    terrain_generate(&ctx->terrain, ctx->map);


  // DisableCursor();

}

/* Custom free camera built on UpdateCameraPro.
 *
 * Movement matches the stock CAMERA_FREE layout: W/S forward and back, A/D
 * strafe, mouse to look. Q/E rise and sink (Space is already bound to
 * regenerating the map, and the arrow keys change the site count).
 * [ and ] change the speed while it runs.
 */
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

  if (ctx->regenerate) {
    regenerate_map(ctx);
  }

  if (ctx->draw_mode == MODE_3D) {
    update_free_camera(ctx);
  } 

}

void gen_ctx_draw(GenerationCtx *ctx) {

  ClearBackground(BLACK);
    
  switch (ctx->draw_mode) {
    case MODE_3D:
      BeginMode3D(ctx->camera);
        terrain_draw(&ctx->terrain);
      EndMode3D();
      DrawText(TextFormat("camera speed: %.0f  ([ / ])", ctx->camera_speed), 10, 10, 20, RAYWHITE);
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

}

