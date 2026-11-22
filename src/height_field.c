#include "height_field.h"
#include "map_gen_height.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>

#include <raymath.h>

/* The terrain mesh samples a cached float field instead of evaluating the
 * Delaunay interpolant at every vertex. Sampling every 2 map pixels keeps the
 * cache small; the blur hides the C0 creases of the barycentric field, which
 * would otherwise make every Delaunay triangle a flat-shaded plane. */
#define HEIGHT_FIELD_STEP 2
#define HEIGHT_FIELD_BLUR_SCALE 0.30f /* blur sigma as a fraction of site spacing */
#define HEIGHT_FIELD_BLUR_PASSES 2

void height_field_free(THeightField *field) {
  if (!field) return;
  free(field->data);
  field->data = NULL;
  field->width = 0;
  field->height = 0;
  field->step = 0;
}

/* Separable box blur with clamped edges, so the border keeps its value. */
static void height_field_box_blur(float *data, float *tmp, int w, int h, int radius) {
  int span = 2 * radius + 1;

  for (int y = 0; y < h; y++) {
    float *row = &data[(size_t)y * w];
    float sum = 0.0f;
    for (int x = -radius; x <= radius; x++) {
      int sx = x < 0 ? 0 : (x >= w ? w - 1 : x);
      sum += row[sx];
    }
    for (int x = 0; x < w; x++) {
      tmp[(size_t)y * w + x] = sum / span;
      int out = x - radius;
      int in = x + radius + 1;
      if (out < 0) out = 0;
      if (in >= w) in = w - 1;
      sum += row[in] - row[out];
    }
  }

  for (int x = 0; x < w; x++) {
    float sum = 0.0f;
    for (int y = -radius; y <= radius; y++) {
      int sy = y < 0 ? 0 : (y >= h ? h - 1 : y);
      sum += tmp[(size_t)sy * w + x];
    }
    for (int y = 0; y < h; y++) {
      data[(size_t)y * w + x] = sum / span;
      int out = y - radius;
      int in = y + radius + 1;
      if (out < 0) out = 0;
      if (in >= h) in = h - 1;
      sum += tmp[(size_t)in * w + x] - tmp[(size_t)out * w + x];
    }
  }
}

/* Rasterise the map height field once (instead of per chunk) and smooth it. */
void height_field_build(THeightField *field, MapGraph *map) {
  height_field_free(field);
  if (!map || map->nc <= 0) return;

  int step = HEIGHT_FIELD_STEP;
  int w = (int)(map->texture_size.x / step) + 1;
  int h = (int)(map->texture_size.y / step) + 1;
  if (w < 2 || h < 2) return;

  float *data = (float *)malloc((size_t)w * h * sizeof(float));
  if (!data) return;

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      data[(size_t)y * w + x] = map_heights_evaluate(
          map, (Vector2){ (float)(x * step), (float)(y * step) });
    }
  }

  float *tmp = (float *)malloc((size_t)w * h * sizeof(float));
  if (tmp) {
    /* Keep the blur proportional to the Delaunay triangle size, so the field
     * stays smooth when the user changes the number of sites. Two box passes
     * give the kernel a C1 profile. */
    int radius = (int)(map->spacing * HEIGHT_FIELD_BLUR_SCALE / step + 0.5f);
    if (radius < 1) radius = 1;

    for (int p = 0; p < HEIGHT_FIELD_BLUR_PASSES; p++)
      height_field_box_blur(data, tmp, w, h, radius);
    free(tmp);
  }

  field->data = data;
  field->width = w;
  field->height = h;
  field->step = step;
}

float height_field_sample(const THeightField *field, float x, float y) {
  if (!field || !field->data || field->width < 2 || field->height < 2 ||
      field->step <= 0)
    return 0.0f;

  float fx = x / field->step;
  float fy = y / field->step;
  int x0 = (int)floorf(fx);
  int y0 = (int)floorf(fy);
  float tx = fx - (float)x0;
  float ty = fy - (float)y0;
  int x1 = x0 + 1;
  int y1 = y0 + 1;

  if (x0 < 0) x0 = 0; else if (x0 >= field->width) x0 = field->width - 1;
  if (x1 < 0) x1 = 0; else if (x1 >= field->width) x1 = field->width - 1;
  if (y0 < 0) y0 = 0; else if (y0 >= field->height) y0 = field->height - 1;
  if (y1 < 0) y1 = 0; else if (y1 >= field->height) y1 = field->height - 1;

  float a = field->data[(size_t)y0 * field->width + x0];
  float b = field->data[(size_t)y0 * field->width + x1];
  float c = field->data[(size_t)y1 * field->width + x0];
  float d = field->data[(size_t)y1 * field->width + x1];

  float top = a + (b - a) * tx;
  float bottom = c + (d - c) * tx;
  return top + (bottom - top) * ty;
}

Vector3 height_field_normal(const THeightField *field, float x, float y,
                            float height_power) {
  if (!field || !field->data || field->step <= 0) return (Vector3){ 0, 1, 0 };

  float e = (float)field->step;
  float hl = height_field_sample(field, x - e, y);
  float hr = height_field_sample(field, x + e, y);
  float hd = height_field_sample(field, x, y - e);
  float hu = height_field_sample(field, x, y + e);

  Vector3 n = {
    (hl - hr) * height_power / (2.0f * e),
    1.0f,
    (hd - hu) * height_power / (2.0f * e),
  };
  return Vector3Normalize(n);
}
