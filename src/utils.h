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

// Arrays

#define INVALID_INDEX 0xffffffff

typedef struct {
  size_t size;
  size_t capacity;
  int *data;
} IntArray;

IntArray int_array_new(size_t initial_capacity);

static int  int_array_at(IntArray *arr, int idx);
static void int_array_push(IntArray *arr, int value);
static int  int_array_shift(IntArray *arr);
static void int_array_free(IntArray *arr);
       void int_array_print(IntArray *a);

// Min Heap

typedef struct { float value; int idx; } KI;
typedef struct { KI *data; int len, cap; } MinHeap;

static void mh_push(MinHeap *h, float k, int i);
static KI   mh_pop(MinHeap *h);

// COLORS

typedef struct {
  float threshold;
  Color color;
} GradientStop;

Color get_percentage_gradient_color(const GradientStop* stops, int stop_count, float ratio);

#endif
