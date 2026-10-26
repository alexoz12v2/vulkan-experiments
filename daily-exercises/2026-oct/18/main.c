#include <stdio.h>

#include "stdint.h"
#include "unistd.h"

// start with NGPR = 0, NFPR = 0
// w0 <- count, NGPR = 1
// x1 <- arr1, NGPR = 2
// x2 <- arr2, NGPR = 3
// return (reset all counters)
// v0 <- low 32 bits, hold result. rest zeroed (128bit wide)
extern float avkDotProduct(int32_t count, float const* arr1, float const* arr2);

static void printArray(int32_t count, float const* arr);

int main(void) {
  setvbuf(stdout, NULL, _IONBF, 0);
  // -O0 shouldn't duplicate the message. Check it
#define INTRO_MESSAGE "Hello World from Apple!\n"
  int8_t const* const introMessage = (int8_t const*)INTRO_MESSAGE;
  size_t const introMessage_length = sizeof(INTRO_MESSAGE) - 1;  // remove '\0'
#undef INTRO_MESSAGE

  write(STDOUT_FILENO, introMessage, introMessage_length);

#define COUNT 15
  float arr1[COUNT] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
  float arr2[COUNT] = {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1};
  puts("Array 1: ");
  printArray(COUNT, arr1);
  putc('\n', stdout);
  puts("Array 2: ");
  printArray(COUNT, arr2);
  putc('\n', stdout);

  // Time to call our dot product function
  puts("Calling avkDotProduct...\n");
  float const theDot = avkDotProduct(COUNT, arr1, arr2);
  printf("The dot product between the two is [%.3f] expected [%.3f]\n", theDot,
         680.f);
#undef COUNT
}

static void printArray(int32_t count, float const* __restrict arr) {
  if (arr == NULL || count <= 0) {
    return;
  }
  write(STDOUT_FILENO, "[ ", 2);
  for (int32_t i = 0; i < count; ++i) {
    float const elem = arr[i];
    // only mix with C libc cause I don't want to format a float right now
    fprintf(stdout, "%.3f", elem);
    fflush(stdout);
    if (i < count - 1) {
      write(STDOUT_FILENO, ", ", 2);
    }
  }
  write(STDOUT_FILENO, " ]", 2);
}
