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

int int_array_at(IntArray *arr, int idx);

void int_array_push(IntArray *arr, int value);

int int_array_shift(IntArray *arr);

void int_array_free(IntArray *arr);

void int_array_print(IntArray *a);

// COLORS

typedef struct {
  float threshold;
  Color color;
} GradientStop;

Color get_percentage_gradient_color(const GradientStop* stops, int stop_count, float ratio);

#endif
