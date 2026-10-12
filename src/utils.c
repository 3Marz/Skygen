#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

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
