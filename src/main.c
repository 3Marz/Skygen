
#include "generation_ctx.h"
#include <raylib.h>

int main(void) {

  GenerationCtx gen_ctx = { 0 };
  gen_ctx_init(&gen_ctx);

  while (!WindowShouldClose()) {

    // Update
    gen_ctx_update(&gen_ctx);

    // Draw
    BeginDrawing();

      gen_ctx_draw(&gen_ctx);

    EndDrawing();

  }

  gen_ctx_free(&gen_ctx);

  CloseWindow();

  return 0;
}
