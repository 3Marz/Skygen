#ifndef SKYGEN_GEN_CTX_H
#define SKYGEN_GEN_CTX_H

#include "map_gen.h"

typedef struct {

  int screen_width;
  int screen_height;

  MapParams map_params;

  MapGraph *map;
  Camera camera;

  int draw_mode;
  int regenerate;

} GenerationCtx;


void gen_ctx_init(GenerationCtx *ctx);
void gen_ctx_update(GenerationCtx *ctx);
void gen_ctx_draw(GenerationCtx *ctx);
void gen_ctx_free(GenerationCtx *ctx);


#endif

