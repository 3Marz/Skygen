
#include "lights.h"
#include "utils.h"
#include <raylib.h>

#define RLIGHTS_IMPLEMENTATION
#include "../include/rlights.h"

void light_ctx_init(LightCtx *ctx) {
  
  ctx->shader = LoadShader("assets/shaders/lighting.vs", "assets/shaders/lighting.fs");
  ctx->shader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(ctx->shader, "viewPos");

  // Ambient light level (some basic lighting)
  int ambientLoc = GetShaderLocation(ctx->shader, "ambient");
  SetShaderValue(ctx->shader, ambientLoc, (float[4]){ 0.3f, 0.3f, 0.3f, 1.0f }, SHADER_UNIFORM_VEC4);

  float p = 100.0f;
  ctx->lights[0] = CreateLight(LIGHT_DIRECTIONAL, v3(p,p,p), v3(0.0f,0.0f,0.0f), WHITE, ctx->shader);

}

void light_ctx_update(LightCtx *ctx, Vector3 camera_pos) {
  int cp[3] = {camera_pos.x, camera_pos.y, camera_pos.z};
  SetShaderValue(ctx->shader, ctx->shader.locs[SHADER_LOC_VECTOR_VIEW], cp, SHADER_UNIFORM_VEC3);
  UpdateLightValues(ctx->shader, ctx->lights[0]);
}

void light_ctx_free(LightCtx *ctx) {
  UnloadShader(ctx->shader);
}

