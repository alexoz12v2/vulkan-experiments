#include <stdint.h>
#include <stdio.h>

extern void avkInsertionSort(int32_t count, int32_t* array);

static void printArray(int32_t count, int32_t* array);

int main(void) {
  // can't remember to turn of IO buffering
  setvbuf(stdout, NULL, _IONBF, 0);

  puts(
      "Hello and welcome to Android ABI aarch64 insertion sort with "
      "assembly!\n  Before sorting: ");
  int32_t arr[] = {32, 4, 7, 18, 0, -4, 43, 32, 1};
  int32_t const count = sizeof(arr) / sizeof(int32_t);
  printArray(count, arr);

  avkInsertionSort(count, arr);

  puts("\n  After sorting: ");
  printArray(count, arr);
  putc('\n', stdout);
}

static void printArray(int32_t count, int32_t* array) {
  if (count <= 0 || array == NULL) {
    return;
  }

  putc('[', stdout);
  putc(' ', stdout);
  for (int32_t i = 0; i < count; i++) {
    printf("%d", array[i]);
    if (i != count - 1) {
      putc(',', stdout);
      putc(' ', stdout);
    } else {
      putc(' ', stdout);
      putc(']', stdout);
    }
  }
}
