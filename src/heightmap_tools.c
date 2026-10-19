#include <math.h>
#include <raylib.h>
#include <raymath.h>
#include <stdio.h>
#include <string.h>
#include "map_gen.h"
#include "utils.c"


void add_hills(MapData *map, int num[], int height[], int x[], int y[]) {

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

void add_pits(MapData *map, int num[], int height[], int x[], int y[]) {

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

void add_elevation(MapData *map, int value, int land_only) {
  for (int i = 0; i < map->nc; i++) {
    if (land_only && map->c[i].elevation < 20) continue;
    map->c[i].elevation = limit_h(map->c[i].elevation + value);
  }
}

IntArray get_cell_range(MapData *map, int cur, int end, float randomnes, int expand_width) {

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
        // if(rnd() > 0.5f) {
        //   c *= 0.8f;
        // } else {
        //   c *= 2.0f;
        // }
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

void add_mount_range(MapData *map, int num[], int length[], int height[], int x[], int y[]) {

  int n_range = rand_range(num);

  for (int i = 0; i < n_range; i++) {

    int len = rand_range(length);
    if (len < 1) len = 1;
    if (len > map->nc) len = map->nc; /* a ridge can't be longer than the map */
    int peak = limit_h(rand_range(height));

    /* Ridge head: a random point inside the requested area. */
    Vector2 randPos = { rand_range(x), rand_range(y) };
    int start = get_closest_center_index(map, randPos);
    if (start < 0) continue;

    /* Walk the spine: at every step follow the neighbour that best continues
     * the current heading. The heading drifts a little so the range curves
     * instead of coming out a ruler line. */
    int path[len];
    path[0] = start;
    int steps = 1;
    int cur = start;
    float ang = rnd() * 2.0f * PI;
    while (steps < len) {
      ang += (rnd() - 0.5f) * 0.7f;
      Vector2 dir = { cosf(ang), sinf(ang) };

      int best = -1;
      float bestDot = 0.3f; /* must still be a forward step */
      for (int ni = 0; ni < map->c[cur].nneigh; ni++) {
        int n = map->c[cur].neigh[ni];
        Vector2 d = Vector2Subtract(map->c[n].pos, map->c[cur].pos);
        float l = Vector2Length(d);
        if (l < 1e-4f) continue;
        float dot = (d.x * dir.x + d.y * dir.y) / l;
        if (dot > bestDot) { bestDot = dot; best = n; }
      }
      if (best < 0) break; /* ran off the graph */

      int seen = 0;
      for (int k = 0; k < steps; k++) if (path[k] == best) { seen = 1; break; }
      if (seen) break; /* the heading folded back onto the spine */

      path[steps++] = best;
      cur = best;
    }

    /* Height along the spine: tallest in the middle, tapering at both ends so
     * the range reads as a ridge and not a wall. */
    float change[map->nc];
    memset(change, 0, sizeof(change));
    for (int k = 0; k < steps; k++) {
      float t = steps > 1 ? (float)k / (float)(steps - 1) : 0.5f;
      float profile = sinf(t * PI);
      if (profile < 0.4f) profile = 0.4f;
      change[path[k]] = (float)peak * profile;
    }

    /* Spread into the flanks: each ring keeps a fraction of the one before it,
     * so the slope (and with it the range width) follows blob_power, the same
     * knob the hills use. The spread is capped at a few rings so the slopes
     * never grow wider than the spine is long - a range should read as a
     * ridge, not a hill. */
    float flank = 0.5f * map->blob_power;
    int maxdepth = 1 + len / 3;
    IntArray queue = int_array_new(1);
    for (int k = 0; k < steps; k++) int_array_push(&queue, path[k]);
    for (int depth = 0; depth < maxdepth && queue.size > 0; depth++) {
      size_t level = queue.size; /* FIFO queue batched by ring = one BFS level */
      for (size_t e = 0; e < level; e++) {

        int q = int_array_shift(&queue);

        for (int ni = 0; ni < map->c[q].nneigh; ni++) {
          int n = map->c[q].neigh[ni];
          if (change[n]) continue;
          change[n] = change[q] * flank * (rnd() * 0.1f + 0.95f);
          if (change[n] > 1.0f) int_array_push(&queue, n);
        }
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

void add_strip(MapData *map, int height[], int width[], int horizontal) {

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





