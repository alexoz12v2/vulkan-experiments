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
  requiredExtensions.push_back(VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
  requiredExtensions.push_back(VK_KHR_UNIFORM_BUFFER_STANDARD_LAYOUT_EXTENSION_NAME);
  requiredExtensions.push_back(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
  requiredExtensions.push_back(VK_KHR_VULKAN_MEMORY_MODEL_EXTENSION_NAME);
  requiredExtensions.push_back(VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME);
  requiredExtensions.push_back(VK_KHR_DESCRIPTOR_UPDATE_TEMPLATE_EXTENSION_NAME);
  requiredExtensions.push_back(VK_KHR_MAINTENANCE_4_EXTENSION_NAME);
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
       default: break;
      }
    }
  }

  return true;
}

}
