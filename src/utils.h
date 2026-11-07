#ifndef SKYGEN_UTILS_H
#define SKYGEN_UTILS_H

#include <stdio.h>
#include <raylib.h>

#define clamp(value, low, high) ((value) < (low) ? (low) : ((value) > (high) ? (high) : (value)))
#define limit_h(value) clamp(value, 0, 100)

float rnd();
int rand_range(int range[]);

int array_includes(int arr[], int size, int target);

typedef enum { T_INT, T_FLOAT, T_CHAR } DataType;

void printv(void* data, DataType type);

// Vectors
Vector2 v2(float x, float y);
Vector3 v3(float x, float y, float z);

// Arrays

#define INVALID_INDEX 0xffffffff

typedef struct {
  size_t size;
  size_t capacity;
  int *data;
} IntArray;

IntArray int_array_new(size_t initial_capacity);

int  int_array_at(IntArray *arr, int idx);
void int_array_push(IntArray *arr, int value);
int  int_array_shift(IntArray *arr);
void int_array_free(IntArray *arr);
void int_array_print(IntArray *a);

// Min Heap

typedef struct { float value; int idx; } KI;
typedef struct { KI *data; int len, cap; } MinHeap;

void mh_push(MinHeap *h, float k, int i);
KI   mh_pop(MinHeap *h);

// KD Tree
typedef struct { Vector2 pos; int idx; } KDPoint;

typedef struct KDNode {
  KDPoint point;
  int axis; //X = 0, Y = 1
  struct KDNode *left;
  struct KDNode *right;
} KDNode;

typedef struct {
  KDNode *root;
  size_t size;
} KDTree;

typedef struct {
  KDPoint point;
  float dist;
} KDNeighbor;

bool kd_tree_build(KDTree *tree, KDPoint *points, size_t count);
bool kd_tree_nearest(KDTree *tree, Vector2 target, KDNeighbor *result);
void kd_tree_free(KDTree *tree);

// COLORS

typedef struct {
  float threshold;
  Color color;
} GradientStop;

Color get_percentage_gradient_color(GradientStop* stops, int stop_count, float ratio);

// Draw
void ImageDrawLineThick(Image *dst, int startPosX, int startPosY, int endPosX, int endPosY, float thickness, Color color);

#endif
