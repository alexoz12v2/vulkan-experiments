// TODO: Translate this into SPIR-V With the Subgroup instructions
#include <cuda_runtime.h>
#include <vector>
#include <iostream>

__global__ void warp_bitonicSort(int* array) {
  int laneid;
  asm volatile("mov.b32 %0, %%laneid;" : "=r"(laneid));

  int value = array[laneid];

  // loop over block size
  for (int blk = 2; blk <= warpSize; blk <<= 1) {
    // loop over partner selection, interleaved from furthest to nearest
    for (int j = blk >> 1; j > 0; j >>= 1) {
      int const partner = laneid ^ j;
      int const partnerValue = __shfl_sync(0xFFFF'FFFFU, value, partner);
      // TODO: translate with a thruth table
      bool const ascending = (laneid & blk) == 0;
      if (laneid < partner) {
        if (ascending) {
          value = min(value, partnerValue);
        } else {
          value = max(value, partnerValue);
        }
      } else {
        if (ascending) {
          value = max(value, partnerValue);
        } else {
          value = min(value, partnerValue);
        }
      }
    }
  }

  array[laneid] = value;
}

#define CUDA_CHECK(res) do { cudaError_t const err = (res); if (err != cudaSuccess) exit(1); } while (0)

int main() {
  CUDA_CHECK(cudaInitDevice(0, 0, 0));
  CUDA_CHECK(cudaSetDevice(0));

  std::vector<int> arr { 
      1, 24, 56, 8, 5, 70, 43, 82, 25, 3, 15, 38,
      15, 57, 31, 67, 10, 22, 26, 17, 91, 100, 45,
      71, 34, 29, 30, 40, 50, 2, 4, 7
  };

  auto const print_arr = [](int const* arr, size_t num) -> std::ostream& {
    std::cout << '{';
    for (size_t i = 0; i < num; ++i)
      std::cout << ' ' << arr[i];
    return std::cout << " }";
  };

  int* d_arr = nullptr;
  CUDA_CHECK(cudaMalloc(&d_arr, sizeof(int) * arr.size()));
  CUDA_CHECK(cudaMemcpy(d_arr, arr.data(), sizeof(int) * arr.size(), cudaMemcpyHostToDevice));

  std::cout << "Before: ";
  print_arr(arr.data(), arr.size()) << std::endl;

  warp_bitonicSort<<<1, 32>>>(d_arr);
  CUDA_CHECK(cudaGetLastError());
  CUDA_CHECK(cudaDeviceSynchronize());

  CUDA_CHECK(cudaMemcpy(arr.data(), d_arr, sizeof(int) * arr.size(), cudaMemcpyDeviceToHost));
  cudaFree(d_arr);

  std::cout << "After: ";
  print_arr(arr.data(), arr.size()) << std::endl;
}

