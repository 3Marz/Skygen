#ifndef SKYGEN_GEN_CTX_H
#define SKYGEN_GEN_CTX_H

#include "lights.h"
#include "map_gen.h"
#include "terrain.h"

#define CHUNCK_SPACE 256
#define MAP_SIZE_X 2048 // Must be divided by CHUNCK_SIZE
#define MAP_SIZE_Y 2048 // Must be divided by CHUNCK_SIZE

typedef struct {
  unsigned int rand_seed;

  int screen_width;
  int screen_height;

  MapParams map_params;

  MapGraph *map;
  Terrain terrain;
  Camera camera;
  float camera_speed; /* free-camera movement speed, world units per second */

  LightCtx lctx;

  int draw_mode;
  int regenerate;

} GenerationCtx;


void gen_ctx_init(GenerationCtx *ctx);
void gen_ctx_update(GenerationCtx *ctx);
void gen_ctx_draw(GenerationCtx *ctx);
void gen_ctx_free(GenerationCtx *ctx);


#endif

