#include <math.h>
#include <raylib.h>
#include <raymath.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "map_gen.h"
#include "utils.c"


void add_hills(MapGraph *map, int num[], int height[], int x[], int y[]) {

  int n_hill = rand_range(num);

  for (int i = 0; i < n_hill; i++) {

    int change[map->nc];
    memset(change, 0, sizeof(change));
    int start;
    int limit = 0;
    int h = rand_range(height);

    do {
      Vector2 randPos = { rand_range(x), rand_range(y) };
      start = get_closest_center_index(map, randPos);
      limit++;
    } while (start >= 0 && map->c[start].elevation + h > 90 && limit < 50);
    if (start < 0) start = 0; /* no centers at all; keep the index valid */

    change[start] = h;
    IntArray queue = int_array_new(4); 
    int_array_push(&queue, start);
    while (queue.size > 0) {
      int q = int_array_shift(&queue);

      for (int ni = 0; ni < map->c[q].nneigh; ni++) {
        int n = map->c[q].neigh[ni];
        if (change[n]) continue;
        change[n] = pow(change[q], map->blob_power) * (rnd() * 0.1f + 0.95f);
        if (change[n] > 1.0f) int_array_push(&queue, n); 
      }
    }
    int_array_free(&queue);

    for (int j = 0; j < map->nc; j++) {
      if (change[j]) {
        map->c[j].elevation = limit_h(map->c[j].elevation + change[j]);
      }
    }


  }
}

void add_pits(MapGraph *map, int num[], int height[], int x[], int y[]) {

  int n_pit = rand_range(num);

  for (int i = 0; i < n_pit; i++) {

    int used[map->nc];
    memset(used, 0, sizeof(used));
    int start;
    int limit = 0;
    int h = limit_h(rand_range(height));

    do {
      Vector2 randPos = { rand_range(x), rand_range(y) };
      start = get_closest_center_index(map, randPos);
      limit++;
    } while (map->c[start].elevation < 20 && limit < 50);
    // if (start < 0) start = 0; /* no centers at all; keep the index valid */

    IntArray queue = int_array_new(4);
    int_array_push(&queue, start);

    while (queue.size > 0) {
      int q = int_array_shift(&queue);

      h = pow(h, map->blob_power) * (rnd() * 0.2f + 0.9f);
      // map->c[q].elevation = limit_h(map->c[q].elevation - h * (rnd() * 0.2f + 0.9f));
      // used[q] = 1;
      if (h < 1) continue;

      for (int ni = 0; ni < map->c[q].nneigh; ni++) {
        int n = map->c[q].neigh[ni];
        if (used[n]) continue;
        map->c[n].elevation = limit_h(map->c[n].elevation - h * (rnd() * 0.2f + 0.9f));
        used[n] = 1;
        int_array_push(&queue, n); 
      }
    }

    int_array_free(&queue);

  }

}

void add_elevation(MapGraph *map, int value, int land_only) {
  for (int i = 0; i < map->nc; i++) {
    if (land_only && map->c[i].elevation < 20) continue;
    map->c[i].elevation = limit_h(map->c[i].elevation + value);
  }
}

void multiply_elevation(MapGraph *map, float value, int height_mask[]) {
  for (int i = 0; i < map->nc; i++) {
    int h = map->c[i].elevation;
    if (h >= height_mask[0] && h <= height_mask[1]) {
      map->c[i].elevation = limit_h(map->c[i].elevation * value);
    }
  }
}

void mask_elevation(MapGraph *map, float power) {
    float abs_power = fabsf(power);
    float fr = (abs_power != 0.0f) ? abs_power : 1.0f;

    for (int i = 0; i < map->nc; i++) {
        float h = map->c[i].elevation;
        Vector2 pos = map->c[i].pos;

        // Normalized coordinates: [-1, 1], where 0 is the center
        float nx = (2.0f * pos.x) / map->texture_size.x - 1.0f;
        float ny = (2.0f * pos.y) / map->texture_size.y - 1.0f;

        // 1 is center, 0 is edge
        float dist = (1.0f - (nx * nx)) * (1.0f - (ny * ny));

        // Invert if power is negative (0 is center, 1 is edge)
        if (power < 0) dist = 1.0f - dist;

        float masked = h * dist;

        map->c[i].elevation = limit_h((h * (fr - 1.0f) + masked) / fr);
    }
}

void invert_elevation(MapGraph *map, int rate) {
  for (int i = 0; i < map->nc; i++) {
    map->c[i].elevation = limit_h(rate - map->c[i].elevation);
  }
}

void slop_elevation(MapGraph *map, float power, int deg_angle) {
    float abs_power = fabsf(power);
    float fr = (abs_power != 0.0f) ? abs_power : 1.0f;

    float angle = deg_angle * DEG2RAD;

    Vector2 dir = {
      .x = cosf(angle),
      .y = sinf(angle)
    };

    for (int i = 0; i < map->nc; i++) {
        float h = map->c[i].elevation;
        Vector2 pos = map->c[i].pos;

        // Normalized coordinates: [-1, 1], where 0 is the center
        float nx = (2.0f * pos.x) / map->texture_size.x - 1.0f;
        float ny = (2.0f * pos.y) / map->texture_size.y - 1.0f;

        float proj = (nx * dir.x) + (ny * dir.y);
        float dist = 0.5f * (proj + 1.0f);

        // Invert if power is negative (0 is center, 1 is edge)
        if (power < 0) dist = 1.0f - dist;

        float masked = h * dist;

        map->c[i].elevation = limit_h((h * (fr - 1.0f) + masked) / fr);
    }
}

void smooth_elevation(MapGraph *map, int passes) {

  for (int p = 0; p < passes; p++) {
    int heights[map->nc];
    memset(&heights, 0, sizeof(heights));

    for (int c = 0; c < map->nc; c++) {
      int avg = map->c[c].elevation;
      for (int ni = 0; ni < map->c[c].nneigh; ni++) {
        int n = map->c[c].neigh[ni];
        avg += map->c[n].elevation;
      }
      avg = avg / (map->c[c].nneigh+1);

      heights[c] = avg;
    }

    for (int i = 0; i < map->nc; i++) {
      map->c[i].elevation = limit_h(heights[i]);
    }
  }
}

IntArray get_cell_range(MapGraph *map, int cur, int end, float randomnes, int expand_width) {

  IntArray range = int_array_new(4);
  int_array_push(&range, cur);

  int used[map->nc];
  memset(&used, 0, sizeof(used));

  float min_d = INFINITY;

  while (cur != end) {
    int next = -1;
    for (int i = 0; i < map->c[cur].nneigh; i++) {
      int n = map->c[cur].neigh[i];
      if (used[n] && min_d != INFINITY) continue;

      float diff = Vector2Distance(map->c[n].pos, map->c[end].pos);
      float c = diff;

      if (rnd() > 1.0f - randomnes) {
        c *= 0.8f;
      }

      if (c < min_d) {
        min_d = diff;
        next = n;
      }
    }

    if (next == -1) {
      // return range;
      min_d = INFINITY;
      for (int i = 0; i < map->c[cur].nneigh; i++) {
        int n = map->c[cur].neigh[i];
        used[n] = 0;
      }
    } else {
      int_array_push(&range, next);
      used[next] = 1;
      cur = next;
    }

  }

  int s_len = range.size * expand_width;
  for (int i = 0; i < s_len; i++) {
    int c = int_array_at(&range, i);
    for (int j = 0; j < map->c[c].nneigh; j++) {
      int n = map->c[c].neigh[j];
      if (used[n]) continue;

      int_array_push(&range, n);

      used[n] = 1;
    }
  }

  return range;
}

void add_mount_range(MapGraph *map, int num[], int length[], int height[], int x[], int y[]) {

  int n_range = rand_range(num);

  for (int i = 0; i < n_range; i++) {

    // Getting the start, end and range
    int len = rand_range(length) * map->spacing;
    if (len < 1) len = 1;
    if (len > map->nc) len = map->nc; /* a ridge can't be longer than the map */
    int h = limit_h(rand_range(height));

    /* Ridge head: a random point inside the requested area. */
    int start, end;
    Vector2 start_pos = { rand_range(x), rand_range(y) };

    float dir_angle = GetRandomValue(0, 360) * (PI/180.0f);
    Vector2 move_vector = {
      .x = (cos(dir_angle)) * len,
      .y = (sin(dir_angle)) * len
    };

    Vector2 end_pos = Vector2Add(start_pos, move_vector);

    start_pos = Vector2Clamp(start_pos, (Vector2){0, 0}, map->texture_size);
    end_pos = Vector2Clamp(end_pos, (Vector2){0, 0}, map->texture_size);

    start = get_closest_center_index(map, start_pos);
    end = get_closest_center_index(map, end_pos);
    if (start < 0 || end < 0) continue;

    IntArray range = get_cell_range(map, start, end, 0.2f, 0);
    IntArray queue = int_array_new(range.size);

    int change[map->nc];
    memset(&change, 0, sizeof(change));

    for (int i = 0; i < range.size; i++) {
      int c = int_array_at(&range, i);
      change[c] = pow(h, map->blob_power) * (rnd() * 0.1f + 0.95f);;
      int_array_push(&queue, c);
    }

    float flank = 0.6f * map->blob_power;
    while (queue.size > 0) {
      size_t level = queue.size; /* FIFO queue batched by ring = one BFS level */
      for (size_t e = 0; e < level; e++) {

        int q = int_array_shift(&queue);

        for (int ni = 0; ni < map->c[q].nneigh; ni++) {
          int n = map->c[q].neigh[ni];
          if (change[n]) continue;
          change[n] = change[q] * flank * (rnd() * 0.1f + 0.95f);
          if (change[n] > 1) int_array_push(&queue, n);
        }
      }
    }
    int_array_free(&queue);
    
    for (int i = 0; i < map->nc; i++) {
      if (change[i]){
        map->c[i].elevation = limit_h(map->c[i].elevation + change[i]);
      }
    }
  }
}

void add_through_range(MapGraph *map, int num[], int length[], int height[], int x[], int y[]) {

  int n_range = rand_range(num);

  for (int i = 0; i < n_range; i++) {

    // Getting the start, end and range
    int len = rand_range(length) * map->spacing;
    if (len < 1) len = 1;
    if (len > map->nc) len = map->nc; /* a ridge can't be longer than the map */
    int h = limit_h(rand_range(height));

    /* Ridge head: a random point inside the requested area. */
    int start, end;
    Vector2 start_pos = { rand_range(x), rand_range(y) };

    float dir_angle = GetRandomValue(0, 360) * (PI/180.0f);
    Vector2 move_vector = {
      .x = (cos(dir_angle)) * len,
      .y = (sin(dir_angle)) * len
    };

    Vector2 end_pos = Vector2Add(start_pos, move_vector);

    start_pos = Vector2Clamp(start_pos, (Vector2){0, 0}, map->texture_size);
    end_pos = Vector2Clamp(end_pos, (Vector2){0, 0}, map->texture_size);

    start = get_closest_center_index(map, start_pos);
    end = get_closest_center_index(map, end_pos);
    if (start < 0 || end < 0) continue;

    IntArray range = get_cell_range(map, start, end, 0.2f, 0);
    IntArray queue = int_array_new(range.size);

    int change[map->nc];
    memset(&change, 0, sizeof(change));

    for (int i = 0; i < range.size; i++) {
      int c = int_array_at(&range, i);
      change[c] = pow(h, map->blob_power) * (rnd() * 0.1f + 0.95f);;
      int_array_push(&queue, c);
    }

    float flank = 0.5f * map->blob_power;
    while (queue.size > 0) {
      size_t level = queue.size; /* FIFO queue batched by ring = one BFS level */
      for (size_t e = 0; e < level; e++) {

        int q = int_array_shift(&queue);

        for (int ni = 0; ni < map->c[q].nneigh; ni++) {
          int n = map->c[q].neigh[ni];
          if (change[n]) continue;
          change[n] = change[q] * flank * (rnd() * 0.1f + 0.95f);
          if (change[n] > 1) int_array_push(&queue, n);
        }
      }
    }
    int_array_free(&queue);
    
    for (int i = 0; i < map->nc; i++) {
      if (change[i]){
        map->c[i].elevation = limit_h(map->c[i].elevation - change[i]);
      }
    }
  }
}

void add_strip(MapGraph *map, int height[], int width[], int horizontal) {

  int start, end;
  Vector2 start_pos, end_pos = { 0 };

  if (horizontal) {
    start_pos = (Vector2){ 0,                   GetRandomValue((int)(map->texture_size.y/4), (int)(map->texture_size.y*3/4)) };
    end_pos   = (Vector2){ map->texture_size.x, GetRandomValue((int)(map->texture_size.y/4), (int)(map->texture_size.y*3/4)) };
  } else {
    start_pos = (Vector2){ GetRandomValue((int)(map->texture_size.x/4), (int)(map->texture_size.x*3/4)), 0                   };
    end_pos   = (Vector2){ GetRandomValue((int)(map->texture_size.x/4), (int)(map->texture_size.x*3/4)), map->texture_size.y };
  }

  start = get_closest_center_index(map, start_pos);
  end = get_closest_center_index(map, end_pos);

  IntArray range = get_cell_range(map, start, end, 0.1f, rand_range(width));
  IntArray queue = int_array_new(range.size);

  int h = limit_h(rand_range(height));
  int change[map->nc];
  memset(&change, 0, sizeof(change));

  for (int i = 0; i < range.size; i++) {
    int c = int_array_at(&range, i);
    change[c] = pow(h, map->blob_power) * (rnd() * 0.1f + 0.95f);;
    int_array_push(&queue, c);
  }

  float flank = 0.5f * map->blob_power;
  while (queue.size > 0) {
    size_t level = queue.size; /* FIFO queue batched by ring = one BFS level */
    for (size_t e = 0; e < level; e++) {

      int q = int_array_shift(&queue);

      for (int ni = 0; ni < map->c[q].nneigh; ni++) {
        int n = map->c[q].neigh[ni];
        if (change[n]) continue;
        change[n] = change[q] * flank * (rnd() * 0.1f + 0.95f);
        if (change[n] > 1) int_array_push(&queue, n);
      }
    }
  }
  int_array_free(&queue);
  
  for (int i = 0; i < map->nc; i++) {
    if (change[i]){
      map->c[i].elevation = limit_h(map->c[i].elevation - change[i]);
    }
  }

}

// Make Cell type either water, land, ocean
void generate_heightmap(MapGraph *map) {
  if (map->nc <= 0) return;

  for (int i = 0; i < map->nc; i++) {
    map->c[i].elevation = 0; // Base height (ocean)
  }

  //Normal Continent
  add_hills(map, (int[]){8, 12}, (int[]){50, 80}, (int[]){20, map->texture_size.x/2}, (int[]){50, map->texture_size.y-50});
  add_hills(map, (int[]){8, 12}, (int[]){50, 80}, (int[]){map->texture_size.x/2, map->texture_size.x-20}, (int[]){50, map->texture_size.y-50});
  add_pits(map, (int[]){3, 5}, (int[]){60, 100}, (int[]){50, map->texture_size.x-50}, (int[]){50, map->texture_size.y-50});
  add_mount_range(map, (int[]){1, 4}, (int[]){8, 40}, (int[]){40, 80}, (int[]){50, map->texture_size.x/2}, (int[]){50, map->texture_size.y-50});
  add_mount_range(map, (int[]){1, 4}, (int[]){8, 40}, (int[]){40, 80}, (int[]){map->texture_size.x/2, map->texture_size.x-50}, (int[]){50, map->texture_size.y-50});
  add_through_range(map, (int[]){1, 2}, (int[]){8, 40}, (int[]){45, 50}, (int[]){50, map->texture_size.x-50}, (int[]){50, map->texture_size.y-50});
  add_strip(map, (int[]){30, 60}, (int[]){2, 6}, 0);
  multiply_elevation(map, 0.9, (int[]){80, 100});
  mask_elevation(map, 1.5f);
  // smooth_elevation(map, 1);
  // slop_elevation(map, 1.0f, GetRandomValue(0, 360));

  //Invert Miss
  // add_elevation(map, 40, 0);
  // mask_elevation(map, 0.2f);
  // add_hills(map, (int[]){10, 15}, (int[]){50, 80}, (int[]){50, map->texture_size.x-50}, (int[]){50, map->texture_size.y-50});
  // add_pits(map, (int[]){3, 5}, (int[]){60, 100}, (int[]){50, map->texture_size.x-50}, (int[]){50, map->texture_size.y-50});
  // add_mount_range(map, (int[]){1, 4}, (int[]){8, 40}, (int[]){40, 50}, (int[]){50, map->texture_size.x-50}, (int[]){50, map->texture_size.y-50});
  // add_through_range(map, (int[]){1, 2}, (int[]){8, 40}, (int[]){45, 50}, (int[]){50, map->texture_size.x-50}, (int[]){50, map->texture_size.y-50});
  // multiply_elevation(map, 0.8, (int[]){20, 100});
  // multiply_elevation(map, 0.7, (int[]){90, 100});
  // invert_elevation(map, 80);

}



