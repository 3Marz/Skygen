#include <math.h>
#include <raylib.h>
#include <string.h>
#include "map_gen.h"
#include "utils.c"


void add_hills(MapData *map, int num[], int height[], int x[], int y[]) {

  int n_hill = GetRandomValue(num[0], num[1]);

  for (int i = 0; i < n_hill; i++) {

    int change[map->nc];
    memset(change, 0, sizeof(change));
    int start;
    int limit = 0;
    int h = GetRandomValue(height[0], height[1]);

    do {
      Vector2 randPos = { GetRandomValue(x[0], x[1]), GetRandomValue(y[0], y[1]) };
      start = getClosestCenterIndex(map, randPos);
      limit++;
    } while (start >= 0 && map->c[start].elevation + h > 90 && limit < 50);
    if (start < 0) start = 0; /* no centers at all; keep the index valid */

    change[start] = h;
    IntArray queue; 
    int_array_init(&queue, 1);
    int_array_push(&queue, start);
    while (queue.size > 0) {
      int q;
      int_array_shift(&queue, &q);

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


// void add_pits(MapData *map, int num[], int height[], int x[], int y[]) {
//
//   int n_pit = GetRandomValue(num[0], num[1]);
//
//   for (int i = 0; i < n_pit; i++) {
//
//     int change[map->nc];
//     memset(change, 0, sizeof(change));
//     int start;
//     int limit = 0;
//     int h = GetRandomValue(height[0], height[1]);
//
//     do {
//       Vector2 randPos = { GetRandomValue(x[0], x[1]), GetRandomValue(y[0], y[1]) };
//       start = getClosestCenterIndex(map, randPos);
//       limit++;
//     } while (map->c[start].elevation < 20 && limit < 50);
//     // if (start < 0) start = 0; /* no centers at all; keep the index valid */
//
//     IntArray queue; 
//     int_array_init(&queue, 1);
//     int_array_push(&queue, start);
//     while (queue.size > 0) {
//       int q;
//       int_array_shift(&queue, &q);
//
//       h = pow(h, map->blob_power) * (rnd() * 0.1f + 0.95f);
//       if (h < 1) return;
//
//       for (int ni = 0; ni < map->c[q].nneigh; ni++) {
//         int n = map->c[q].neigh[ni];
//         if (change[n]) return;
//         map->c[n].elevation = limit_h(map->c[n].elevation - h * (rnd() * 0.1f + 0.95f));
//         change[n] = 1;
//         int_array_push(&queue, n); 
//       }
//     }
//
//     int_array_free(&queue);
//
//   }
// }

void add_pits(MapData *map, int num[], int height[], int x[], int y[]) {

  int n_pits = GetRandomValue(num[0], num[1]);

  for (int i = 0; i < n_pits; i++) {

    int change[map->nc];
    memset(change, 0, sizeof(change));
    int start;
    int limit = 0;
    int h = GetRandomValue(height[0], height[1]);

    do {
      Vector2 randPos = { GetRandomValue(x[0], x[1]), GetRandomValue(y[0], y[1]) };
      start = getClosestCenterIndex(map, randPos);
      limit++;
    } while (start >= 0 && map->c[start].elevation > 20 && limit < 50);
    if (start < 0) start = 0; /* no centers at all; keep the index valid */

    change[start] = h;
    IntArray queue; 
    int_array_init(&queue, 1);
    int_array_push(&queue, start);
    while (queue.size > 0) {
      int q;
      int_array_shift(&queue, &q);

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
        map->c[j].elevation = limit_h(map->c[j].elevation - change[j]);
      }
    }


  }
}


