#include "utils.h"

#include <math.h>
#include <stdio.h>
#include <raylib.h>
#include <raymath.h>
#include <stdlib.h>

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

IntArray int_array_new(size_t initial_capacity) {
  IntArray arr = { 0 };
  arr.size = 0;
  arr.capacity = initial_capacity;
  arr.data = calloc(arr.capacity, sizeof(int));
  return arr;
}

int int_array_at(IntArray *arr, int idx) {
  if (idx >= arr->size || idx >= arr->capacity) {
    printf("Error: Accessing uninitialize memory on int array with size: %lu, index:%i\n", arr->size, idx);
    return INVALID_INDEX;
  }
  return arr->data[idx];
}

void int_array_push(IntArray *arr, int value) {
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

void int_array_print(IntArray *arr) {
    printf("[");
    for (int i = 0; i < arr->size; i++) {
        printf("%d, ", int_array_at(arr, i));
    }
    printf("]\n");
}

// Min Heap
void mh_push(MinHeap *h, float k, int i)
{
    if (h->len == h->cap) { 
      h->cap = h->cap ? h->cap * 2 : 1024;
      h->data = realloc(h->data, (size_t)h->cap * sizeof(KI));
    }

    int c = h->len++;
    h->data[c].value = k; h->data[c].idx = i;
    while (c > 0) {
        int p = (c - 1) / 2;
        if (h->data[p].value <= h->data[c].value) break;
        KI t = h->data[p]; 
        h->data[p] = h->data[c];
        h->data[c] = t; c = p;
    }
}
KI mh_pop(MinHeap *h)
{
    KI top = h->data[0];
    h->data[0] = h->data[--h->len];
    int c = 0;
    for (;;) {
        int l = 2 * c + 1,
            r = l + 1,
            m = c;
        if (l < h->len && h->data[l].value < h->data[m].value) m = l;
        if (r < h->len && h->data[r].value < h->data[m].value) m = r;
        if (m == c) break;
        KI t = h->data[m];
        h->data[m] = h->data[c];
        h->data[c] = t; c = m;
    }
    return top;
}

// COLORS

Color get_percentage_gradient_color(GradientStop* stops, int stop_count, float ratio) {
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

    return result;
}

// Draw
void ImageDrawLineThick(Image *dst, int startPosX, int startPosY, int endPosX, int endPosY, float thickness, Color color) {
  // Calculate differences in coordinates
  int shortLen = endPosY - startPosY;
  int longLen = endPosX - startPosX;
  bool yLonger = false;

  // Determine if the line is more vertical than horizontal
  if (abs(shortLen) > abs(longLen)) {
    // Swap the lengths if the line is more vertical
    int temp = shortLen;
    shortLen = longLen;
    longLen = temp;
    yLonger = true;
  }

  // Initialize variables for drawing loop
  int endVal = longLen + 1;
  int sgnInc = 1;

  // Adjust direction increment based on longLen sign
  if (longLen < 0) {
    longLen = -longLen;
    sgnInc = -1;
    endVal -= 2;
  }

  // Calculate fixed-point increment for shorter length
  int decInc = (longLen == 0) ? 0 : (shortLen << 16) / longLen;

  // Draw the line pixel by pixel
  if (yLonger) {
    // If line is more vertical, iterate over y-axis
    // Init j with 0.5 in 16-bit fixed point (1 << 15) for better rounding.
    for (int i = 0, j = (1 << 15); i != endVal; i += sgnInc, j += decInc) {
      // Calculate pixel position and draw it
      ImageDrawCircle(dst, startPosX + (j >> 16), startPosY + i, thickness, color);
    }
  } else {
    // If line is more horizontal, iterate over x-axis
    for (int i = 0, j = (1 << 15); i != endVal; i += sgnInc, j += decInc) {
      // Calculate pixel position and draw it
      ImageDrawCircle(dst, startPosX + i, startPosY + (j >> 16), thickness, color);
    }
  }
}







