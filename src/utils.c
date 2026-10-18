#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <raylib.h>


#ifndef UTILS_H 
#define UTILS_H

#define clamp(value, low, high) ((value) < (low) ? (low) : ((value) > (high) ? (high) : (value)))
#define limit_h(value) clamp(value, 0, 100)

float rnd() {
  return (float)GetRandomValue(0, 1000000) / 1000000.0f;
}

int rand_range(int range[]) {
  return GetRandomValue(range[0], range[1]);
}

int array_includes(int arr[], int size, int target) {
    for (int i = 0; i < size; i++) {
        if (arr[i] == target) {
            return 1; // Value found immediately
        }
    }
    return 0; // Iterated through entire array without finding a match
}

typedef enum { T_INT, T_FLOAT, T_CHAR } DataType;

void printv(void* data, DataType type) {
    switch (type) {
        case T_INT:
            printf("Integer: %d\n", *(int*)data); // Cast to int* and dereference
            break;
        case T_FLOAT:
            printf("Float: %.2f\n", *(float*)data); // Cast to float* and dereference
            break;
        case T_CHAR:
            printf("Char: %c\n", *(char*)data); // Cast to char* and dereference
            break;
    }
}

// Arrays

#define INVALID_INDEX 0xffffffff

typedef struct {
  size_t size;
  size_t capacity;
  int *data;
} IntArray;

IntArray int_array_new(size_t initial_capacity) {
  IntArray arr = { 0 };
  arr.size = 0;
  arr.capacity = initial_capacity;
  arr.data = calloc(arr.capacity, sizeof(int));
  return arr;
}

int int_array_at(IntArray *arr, int idx) {
  if (idx >= arr->size || idx >= arr->capacity) {
    printf("Error: Accessing uninitialize memory on int array with size: %d, index:%i\n", arr->size, idx);
    return INVALID_INDEX;
  }
  return arr->data[idx];
}

int int_array_push(IntArray *arr, int value) {
  if (arr->size >= arr->capacity) {
    arr->capacity *= 2;
    arr->data = realloc(arr->data, arr->capacity * sizeof(int));
  }
  arr->data[arr->size++] = value;
}

int int_array_shift(IntArray *arr) {
  if (arr->size <= 0) {
    printf("Error: Cant shift on empty array");
    return INVALID_INDEX;
  }

  int out = int_array_at(arr, 0);

  for (int i = 0; i < arr->size; i++) {
    arr->data[i] = arr->data[i+1];
  }

  arr->size--;
  return out;
}

void int_array_free(IntArray *arr) {
  free(arr->data);
  arr->data = NULL;
  arr->capacity = 0;
  arr->size = 0;
}

void int_array_print(IntArray *a) {
    printf("[");
    for (int i = 0; i < a->size; i++) {
        printf("%d, ", int_array_at(a, i));
    }
    printf("]\n");
}

// COLORS

typedef struct {
  float threshold;
  Color color;
} GradientStop;

Color get_percentage_gradient_color(const GradientStop* stops, int stop_count, float ratio) {
    // Boundary protection
    if (ratio <= stops[0].threshold) return stops[0].color;
    if (ratio >= stops[stop_count - 1].threshold) return stops[stop_count - 1].color;

    // Find the two stops that border our current ratio
    int i = 0;
    for (i = 0; i < stop_count - 1; i++) {
        if (ratio >= stops[i].threshold && ratio <= stops[i+1].threshold) {
            break; // Found the active segment!
        }
    }

    GradientStop start_stop = stops[i];
    GradientStop end_stop   = stops[i + 1];

    // Calculate how far along we are *inside* this specific segment range
    // Local Ratio = (Current - Start) / (End - Start)
    float segment_range = end_stop.threshold - start_stop.threshold;
    float local_ratio = (ratio - start_stop.threshold) / segment_range;

    // Linear interpolation between the target colors
    Color result = ColorLerp(start_stop.color, end_stop.color, local_ratio);
    // result.r = (uint8_t)(start_stop.color.r + (end_stop.color.r - start_stop.color.r) * local_ratio);
    // result.g = (uint8_t)(start_stop.color.g + (end_stop.color.g - start_stop.color.g) * local_ratio);
    // result.b = (uint8_t)(start_stop.color.b + (end_stop.color.b - start_stop.color.b) * local_ratio);

    return result;
}

#endif








