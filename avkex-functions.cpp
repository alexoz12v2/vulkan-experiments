#include "avkex.h"

#include <vector>

using namespace avkex;

namespace avkex {

//  - Buffer Device Address: everybody !mobile should support it.
//  - external synchronization and external memory (careful on promoted ones on 1.1)
//  - (__APPLE__) portability subset
//  - (G) swapcchain, swapchain maintenance1
//  - timeline semaphore
//  - uniform buffer standard layout
//  - vulkan memory model
//  - maintenance4 -> specialization constant for LocalSizeId Execution Mode
//  - TODO Debug
std::vector<char const*> getVulkanMinimalRequiredDeviceExtensions() {
  std::vector<char const*> requiredExtensions;
  requiredExtensions.reserve(64);
  requiredExtensions.push_back("VK_KHR_buffer_device_address");
  requiredExtensions.push_back(VK_KHR_UNIFORM_BUFFER_STANDARD_LAYOUT_EXTENSION_NAME);
  requiredExtensions.push_back(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
  requiredExtensions.push_back(VK_KHR_VULKAN_MEMORY_MODEL_EXTENSION_NAME);
  requiredExtensions.push_back(VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME);
  requiredExtensions.push_back(VK_KHR_DESCRIPTOR_UPDATE_TEMPLATE_EXTENSION_NAME);
  requiredExtensions.push_back(VK_KHR_MAINTENANCE_4_EXTENSION_NAME);
  requiredExtensions.push_back(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME); // required by spirv 1.4
  requiredExtensions.push_back(VK_KHR_SPIRV_1_4_EXTENSION_NAME); // rg VK_KHR_SPIR -g '*.cpp' -g '*.h' --no-ignore
#ifdef _WIN32
  requiredExtensions.push_back(VK_KHR_EXTERNAL_FENCE_WIN32_EXTENSION_NAME);
  requiredExtensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME);
  requiredExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
#else
#  ifndef __APPLE__
  requiredExtensions.push_back(VK_KHR_EXTERNAL_FENCE_FD_EXTENSION_NAME);
  requiredExtensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME);
  requiredExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
#  endif
#  ifdef __APPLE__
  requiredExtensions.push_back(VK_EXT_EXTERNAL_MEMORY_METAL_EXTENSION_NAME);
#  elif defined(__linux__)
  requiredExtensions.push_back(VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME);
#  endif
#endif
#ifdef __APPLE__
  requiredExtensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif

  return requiredExtensions;
}

// kept in sync with `EVulkanOptionalExtensionSupport`
std::vector<char const*> getVulkanOptionalDeviceExtensions() {
  std::vector<char const*> optionalExtensions;
  optionalExtensions.reserve(64);
  optionalExtensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
  optionalExtensions.push_back(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME);
  return optionalExtensions;
}

bool handleRequiredDeviceFeatures(VkPhysicalDeviceFeatures2& features, bool checkMode) {
  struct VkStruct { VkStructureType sType; void* pNext; };
  if (checkMode) {
    // check mode: check whether required features are present
    for (auto* currentType = reinterpret_cast<VkStruct*>(&features.sType); 
         currentType; 
         currentType = reinterpret_cast<VkStruct*>(currentType->pNext)) {
      switch (currentType->sType) {
       case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES:
        if (reinterpret_cast<VkPhysicalDeviceTimelineSemaphoreFeatures*>(currentType)->timelineSemaphore != VK_TRUE) {
          LOG_ERR << "Unsupported Device Feature: timelineSemaphore" <<  LOG_RST << std::endl;
          return false;
        }
        break;
       case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES:
        if (reinterpret_cast<VkPhysicalDeviceBufferDeviceAddressFeatures*>(currentType)->bufferDeviceAddress != VK_TRUE) {
          LOG_ERR << "Unsupported Device Feature: bufferDeviceAddress" <<  LOG_RST << std::endl;
          return false;
        }
        break;
       case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES:
        if (reinterpret_cast<VkPhysicalDeviceUniformBufferStandardLayoutFeatures*>(currentType)->uniformBufferStandardLayout != VK_TRUE) {
          LOG_ERR << "Unsupported Device Feature: uniformBufferStandardLayout" <<  LOG_RST << std::endl;
          return false;
        }
        break;
       case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES:
        if (reinterpret_cast<VkPhysicalDeviceVulkanMemoryModelFeatures*>(currentType)->vulkanMemoryModel != VK_TRUE) {
          LOG_ERR << "Unsupported Device Feature: vulkanMemoryModel" <<  LOG_RST << std::endl;
          return false;
        }
#ifdef __APPLE__
       case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR:
        if (reinterpret_cast<VkPhysicalDeviceVulkanMemoryModelFeatures*>(currentType)->vulkanMemoryModel != VK_TRUE) {
          LOG_ERR << "Unsupported Device Feature: vulkanMemoryModel" <<  LOG_RST << std::endl;
          return false;
        }
#endif
        break;
       default: break;
      }
    }
  } else {
    // fill mode: turn to VK_TRUE the required features
    for (auto* currentType = reinterpret_cast<VkStruct*>(&features.sType); 
         currentType; 
         currentType = reinterpret_cast<VkStruct*>(currentType->pNext)) {
      switch (currentType->sType) {
       case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES:
        reinterpret_cast<VkPhysicalDeviceTimelineSemaphoreFeatures*>(currentType)->timelineSemaphore = VK_TRUE;
        break;
       case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES:
        reinterpret_cast<VkPhysicalDeviceBufferDeviceAddressFeatures*>(currentType)->bufferDeviceAddress = VK_TRUE;
        break;
       case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES:
        reinterpret_cast<VkPhysicalDeviceUniformBufferStandardLayoutFeatures*>(currentType)->uniformBufferStandardLayout = VK_TRUE;
        break;
       case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES:
        reinterpret_cast<VkPhysicalDeviceVulkanMemoryModelFeatures*>(currentType)->vulkanMemoryModel = VK_TRUE;
        break;
#ifdef __APPLE__
       case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR:
        reinterpret_cast<VkPhysicalDevicePortabilitySubsetFeaturesKHR*>(currentType)->events = VK_TRUE;
        break;
#endif
       default: break;
      }
    }
  }

  return true;
}

}
