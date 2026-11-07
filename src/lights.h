#ifndef SKYGEN_LIGHTS_H
#define SKYGEN_LIGHTS_H

#include <raylib.h>

#include "../include/rlights.h"

typedef struct {

  Shader shader;
  Light lights[MAX_LIGHTS];

} LightCtx;

void light_ctx_init(LightCtx *ctx);
void light_ctx_update(LightCtx *ctx, Vector3 camera_pos);
void light_ctx_free(LightCtx *ctx);

#endif
