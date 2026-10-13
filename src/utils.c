#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <raylib.h>

typedef struct {
    int *data;
    size_t size;
    size_t capacity;
} IntArray;

void int_array_init(IntArray *a, size_t initial_capacity) {
    a->data = malloc(initial_capacity * sizeof(int));
    a->size = 0;
    a->capacity = initial_capacity;
}

void int_array_push(IntArray *a, int value) {
    if (a->size >= a->capacity) {
        a->capacity *= 2; // Double the capacity
        a->data = realloc(a->data, a->capacity * sizeof(int)); //
    }
    a->data[a->size++] = value;
}

// 1. POP: Removes and returns the last element
bool int_array_pop(IntArray *a, int *out_value) {
    if (a->size == 0) {
        return false; // Array is empty, nothing to pop
    }
    
    // Grab the last element
    *out_value = a->data[--a->size];
    
    // Optional Optimization: Shrink memory if capacity is largely wasted
    if (a->size > 0 && a->size <= a->capacity / 4) {
        a->capacity /= 2;
        a->data = realloc(a->data, a->capacity * sizeof(int));
    }
    
    return true;
}

// 2. SHIFT: Removes and returns the first element
bool int_array_shift(IntArray *a, int *out_value) {
    if (a->size == 0) {
        return false; // Array is empty, nothing to shift
    }
    
    // Grab the first element
    *out_value = a->data[0];
    
    // Shift all remaining elements one slot to the left
    for (size_t i = 1; i < a->size; i++) {
        a->data[i - 1] = a->data[i];
    }
    
    a->size--; // Decrease size tracker
    
    // Optional Optimization: Shrink memory if capacity is largely wasted
    if (a->size > 0 && a->size <= a->capacity / 4) {
        a->capacity /= 2;
        a->data = realloc(a->data, a->capacity * sizeof(int));
    }
    
    return true;
}

void int_array_free(IntArray *a) {
    free(a->data);
    a->data = NULL;
    a->size = a->capacity = 0;
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










