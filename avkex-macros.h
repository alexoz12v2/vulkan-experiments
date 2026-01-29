#pragma once

// ------------------------------------------------------------------------
// Logging Related Macros (rely on iostream)
// ------------------------------------------------------------------------
#include <iostream>

#define LOG_RED "\033[31m"
#define LOG_YLW "\033[73m"
#define LOG_RST "\033[0m"
#define LOG_APP "[VulkanApp] "
#define LOG_ERR std::cerr << \
  LOG_RED LOG_APP << '[' << __FILE__ << ':' << __LINE__ << "] "
#define LOG_LOG std::cerr << \
  LOG_RST LOG_APP << '[' << __FILE__ << ':' << __LINE__ << "] "
#define AVK_VK_RST(expr) \
  do { \
    VkResult var = (expr); \
    if (var < 0) { \
      printf("[VulkanApp] [%s:%d] fatal vulkan error %d\n", __FILE__, __LINE__, var); \
      exit(1); \
    } \
  } while (0)

// ------------------------------------------------------------------------
// Vulkan Related Macros
// ------------------------------------------------------------------------

// https://docs.vulkan.org/spec/latest/appendices/boilerplate.html#boilerplate-wsi-header
#ifdef __APPLE__

#  define VK_USE_PLATFORM_METAL_EXT

#elif defined(_WIN32)

#  include <Windows.h>
#  define VK_USE_PLATFORM_WIN32_KHR

#elif defined(__linux__)

#  include <wayland-client.h>
#  define VK_USE_PLATFORM_WAYLAND_KHR

#else
#  error "unsupported"
#endif

// needed for portability subset extension name macro
#define VK_ENABLE_BETA_EXTENSIONS
#define VOLK_NO_DEVICE_PROTOTYPES

// VMA Configuration with volk
// https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/quick_start.html#quick_start_project_setup
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0

// VMA Memory Leak report
#define VMA_LEAK_LOG_FORMAT(format, ...) do { \
        printf((format), __VA_ARGS__); \
        printf("\n"); \
    } while(false)

