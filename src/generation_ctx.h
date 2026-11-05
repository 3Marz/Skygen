#ifndef SKYGEN_GEN_CTX_H
#define SKYGEN_GEN_CTX_H

#include "map_gen.h"
#include "terrain.h"

typedef struct {
  unsigned int rand_seed;

  int screen_width;
  int screen_height;

  MapParams map_params;

  MapGraph *map;
  Terrain terrain;
  Camera camera;
  float camera_speed; /* free-camera movement speed, world units per second */

  int draw_mode;
  int regenerate;

} GenerationCtx;


void gen_ctx_init(GenerationCtx *ctx);
void gen_ctx_update(GenerationCtx *ctx);
void gen_ctx_draw(GenerationCtx *ctx);
void gen_ctx_free(GenerationCtx *ctx);


#endif

