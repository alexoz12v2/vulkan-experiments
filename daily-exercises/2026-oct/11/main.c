
#include <stdint.h>
#include <stdio.h>

static void printArray(int32_t const sz, int32_t const* __restrict const arr);

extern void avkInsertionSort(int32_t const sz, int32_t* __restrict const arr);

int main(void) {
  setvbuf(stdout, NULL, _IONBF, 0);
  puts("Welcome to Insertion sort with x_86_64 assembly!\n");

  int32_t arr[] = {32, 10, 4, 43, 4, 23, 21, 10};
  int32_t const count = sizeof(arr) / sizeof(int32_t);
  puts("Starting array:\n  ");
  printArray(count, arr);
  putc('\n', stdout);

  // the call
  avkInsertionSort(count, arr);
  puts("Ending array:\n  ");
  printArray(count, arr);
}

static void printArray(int32_t const sz, int32_t const* const arr) {
  if (sz <= 0 || arr == NULL) {
    return;
  }

  putc('[', stdout);
  putc(' ', stdout);
  for (int32_t i = 0; i < sz; i++) {
    printf("%d", arr[i]);
    if (i != sz - 1) {
      putc(',', stdout);
      putc(' ', stdout);
    }
  }
  putc(' ', stdout);
  putc(']', stdout);
  putc('\n', stdout);
}
