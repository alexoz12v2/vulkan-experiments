#include "avkex-macros.h"

#undef VOLK_NO_DEVICE_PROTOTYPES
#define VOLK_IMPLEMENTATION
#include <volk.h>
// must come after volk
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

// now you can include our header
#include "avkex.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include <iostream>
#include <memory>
#include <unordered_set>
#include <vector>

using namespace avkex;

namespace {

VkInstance createVkInstance(VkDebugUtilsMessengerEXT* messenger);
bool checkRequiredInstanceExtensionsPresent(std::vector<char const*> const& requiredExtensions);
bool checkRequiredLayersPresent(std::vector<char const*> const& requiredLayers);
VkBool32 VKAPI_PTR debugUtilsMessengerCallbackEXT(
    VkDebugUtilsMessageSeverityFlagBitsEXT           messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT                  messageTypes,
    const VkDebugUtilsMessengerCallbackDataEXT*      pCallbackData,
    void*                                            pUserData);
VulkanPhysicalDeviceQueryResult checkEligibleDevice(VkInstance instance, VkPhysicalDevice physicalDevice);

}

namespace avkex {

std::function<VulkanPhysicalDeviceQueryResult(VkInstance,VkPhysicalDevice)> const
  VulkanApp::s_defaultDeviceValidator = checkEligibleDevice;

VulkanApp::VulkanApp() {
  // volk init
  if (volkInitialize() != VK_SUCCESS) {
    LOG_ERR << "volkInitialize() failed" LOG_RST << std::endl;
  }
  // instance and messenger
  m_instance = createVkInstance(&m_messenger);
}

VulkanApp::~VulkanApp() noexcept {
#ifdef AVK_DEBUG
  if (m_messenger) {
    auto const pfnDestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)
      vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT");
    assert(pfnDestroyDebugUtilsMessengerEXT);
    pfnDestroyDebugUtilsMessengerEXT(m_instance, m_messenger, nullptr);
    m_messenger = VK_NULL_HANDLE;
  }
#endif
  if (m_instance) {
    vkDestroyInstance(m_instance, nullptr);
    m_instance = VK_NULL_HANDLE;
  }
  LOG_LOG << "Instance Destroyed" << std::endl;

  // volk finalize 
  volkFinalize();
}


std::vector<VulkanDeviceInfo> VulkanApp::getEligibleDevices(bool sorted, std::function<VulkanPhysicalDeviceQueryResult(VkInstance,VkPhysicalDevice)> const& devValidator) const {
  assert(*this);
  std::vector<VulkanDeviceInfo> result;
  std::vector<VkPhysicalDevice> eligibleDevices;
  eligibleDevices.reserve(16);
  result.reserve(16);
  // enumerate devices
  uint32_t count = 0;
  AVK_VK_RST(vkEnumeratePhysicalDevices(m_instance, &count, nullptr));
  if (!count) {
    return result;
  }

  eligibleDevices.resize(count);
  AVK_VK_RST(vkEnumeratePhysicalDevices(m_instance, &count, eligibleDevices.data()));
  for (VkPhysicalDevice physicalDevice : eligibleDevices) {
    VulkanPhysicalDeviceQueryResult const qResult = devValidator(m_instance, physicalDevice);
    if (qResult) {
      result.push_back({physicalDevice, qResult});
    }
  }
  if (sorted && !result.empty()) {
    std::sort(result.begin(), result.end(), [](VulkanDeviceInfo const& a, VulkanDeviceInfo const& b) {
      return a.queryResult.score > b.queryResult.score;
    });
  }

  return result;
}

}

namespace {

VkInstance createVkInstance(VkDebugUtilsMessengerEXT* messenger) {
  VkInstance instance = VK_NULL_HANDLE;  
  // if an instance was already created for this process, fail.
  if (vkDestroyInstance) return instance;

  VkApplicationInfo appInfo{};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "Whatever";
  appInfo.applicationVersion = VK_MAKE_VERSION(1,0,0);
  appInfo.pEngineName = "whatever";
  appInfo.engineVersion = VK_MAKE_VERSION(1,0,0);
  appInfo.apiVersion = VK_API_VERSION_1_1;

  // extensions
  std::vector<char const*> desiredExtensions;
  desiredExtensions.reserve(64);
  desiredExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#ifdef AVK_DEBUG
  desiredExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

  // VK_EXT_layer_settings for printf inside shaders
#ifdef AVK_VVL
  desiredExtensions.push_back(VK_EXT_LAYER_SETTINGS_EXTENSION_NAME);
#endif
  // enumerate instance extensions. if present, good, otherwise, return
  if (!checkRequiredInstanceExtensionsPresent(desiredExtensions)) {
    LOG_ERR << "Some required instance extensions were not found" LOG_RST << std::endl;
    return instance;
  }
 
  std::vector<char const*> desiredLayers;
#ifdef AVK_VVL
  // TODO: https://registry.khronos.org/VulkanSC/specs/1.0-extensions/man/html/VK_EXT_layer_settings.html
  desiredLayers.reserve(64);
  desiredLayers.push_back("VK_LAYER_KHRONOS_validation");
  // enumerate layers. if present, good, otherwise, warn and continue
  if (!checkRequiredLayersPresent(desiredLayers)) {
    LOG_LOG << "VK_LAYER_KHRONOS_validation not found" << std::endl;
    desiredLayers.clear();
  }

  // VK_EXT_layer_settings for printf inside shaders setup
  // (as long as there's a debug messenger with VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
  char const* enableDebugPrintfSetting_layerEnables = "VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT";
  std::vector<VkLayerSettingEXT> layerSettings;
  layerSettings.reserve(16);
  VkLayerSettingsCreateInfoEXT settingsCreateInfo{};
  settingsCreateInfo.sType = VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT;
  if (desiredLayers.size() > 0) { // VK_LAYER_KHRONOS_validation is there?
    layerSettings.emplace_back();
    VkLayerSettingEXT& enableDebugPrintfSetting = layerSettings.back();
    enableDebugPrintfSetting.pLayerName = "VK_LAYER_KHRONOS_validation";
    enableDebugPrintfSetting.pSettingName = "enables";
    enableDebugPrintfSetting.type = VK_LAYER_SETTING_TYPE_STRING_EXT;
    enableDebugPrintfSetting.valueCount = 1;
    enableDebugPrintfSetting.pValues = &enableDebugPrintfSetting_layerEnables;
  }

  settingsCreateInfo.settingCount = static_cast<uint32_t>(layerSettings.size());
  settingsCreateInfo.pSettings = layerSettings.size() > 0 ? layerSettings.data() : nullptr;
#endif

#ifdef AVK_DEBUG
  // define messenger create info
  VkDebugUtilsMessengerCreateInfoEXT msgCreateInfo{};
  if (messenger) {
    msgCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    msgCreateInfo.messageSeverity = 
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    msgCreateInfo.messageType = 
      VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
      VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    msgCreateInfo.pfnUserCallback = debugUtilsMessengerCallbackEXT;
    msgCreateInfo.pUserData = nullptr;
  }
#endif

  VkInstanceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  void const** ppNext = &createInfo.pNext;
#ifdef AVK_DEBUG
  if (messenger) {
    *ppNext = &msgCreateInfo;
    ppNext = &msgCreateInfo.pNext;
  }
#endif
#ifdef AVK_VVL
  if (layerSettings.size() > 0) {
    *ppNext = &settingsCreateInfo;
    ppNext = &settingsCreateInfo.pNext;
  }
#endif
  // apple specific
  createInfo.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  createInfo.pApplicationInfo = &appInfo;
  createInfo.enabledLayerCount = (uint32_t)desiredLayers.size();
  if (desiredLayers.size() > 0) createInfo.ppEnabledLayerNames = desiredLayers.data();
  createInfo.enabledExtensionCount = (uint32_t)desiredExtensions.size();
  createInfo.ppEnabledExtensionNames = desiredExtensions.data();
  AVK_VK_RST(vkCreateInstance(&createInfo, nullptr, &instance));

#ifdef AVK_DEBUG
  if (messenger) {
    auto const pfnCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)
      vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (pfnCreateDebugUtilsMessengerEXT) {
      AVK_VK_RST(pfnCreateDebugUtilsMessengerEXT(instance, &msgCreateInfo, nullptr, messenger));
    } else {
      LOG_ERR << "vkCreateDebugUtilsMessengerEXT not found" LOG_RST << std::endl;
    }
  }
#endif

  // load _global_ instance function pointers.
  volkLoadInstanceOnly(instance);

  return instance;
}


bool checkRequiredInstanceExtensionsPresent(std::vector<char const*> const& requiredExtensions) {
  std::vector<VkExtensionProperties> presentExtensions;
  uint32_t count = 0;
  AVK_VK_RST(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr));
  presentExtensions.resize(count);
  AVK_VK_RST(vkEnumerateInstanceExtensionProperties(
    nullptr, &count, presentExtensions.data()));
  // check presence
  for (auto const& p : requiredExtensions) {
    auto const it = std::find_if(presentExtensions.cbegin(), presentExtensions.cend(), 
      [&](auto const& curr){ return strcmp(curr.extensionName, p) == 0; });
    if (it == presentExtensions.cend())
      return false;
  }
  return true;
}

bool checkRequiredLayersPresent(std::vector<char const*> const& requiredLayers) {
  std::vector<VkLayerProperties> presentLayers;
  uint32_t count = 0;
  AVK_VK_RST(vkEnumerateInstanceLayerProperties(&count, nullptr));
  presentLayers.resize(count);
  AVK_VK_RST(vkEnumerateInstanceLayerProperties(&count, presentLayers.data()));
  for (auto const& p : requiredLayers) {
    auto const it = std::find_if(presentLayers.cbegin(), presentLayers.cend(),
      [&](auto const& prop){ return strcmp(prop.layerName, p) == 0; });
    if (it == presentLayers.cend()) return false;
  }
  return true;
}

VkBool32 VKAPI_PTR debugUtilsMessengerCallbackEXT(
    VkDebugUtilsMessageSeverityFlagBitsEXT           messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT                  messageTypes,
    const VkDebugUtilsMessengerCallbackDataEXT*      pCallbackData,
    void*                                            pUserData) {
  // TODO better
  if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    LOG_ERR << "[Debug Messenger] " << pCallbackData->pMessage << LOG_RST << std::endl;
  } else {
    LOG_LOG << "[Debug Messenger] " << pCallbackData->pMessage << std::endl;
  }
  // the application should always return VK_FALSE.
  return VK_FALSE;
}

VulkanPhysicalDeviceQueryResult checkEligibleDevice(VkInstance instance, VkPhysicalDevice physicalDevice) {
  VulkanPhysicalDeviceQueryResult result{};
  int32_t theScore = 1;
  // properties
  // - probably I'll be needing subgroup information
  VkPhysicalDeviceProperties2 props{};
  props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  vkGetPhysicalDeviceProperties2(physicalDevice, &props);
  LOG_LOG << "Examining Physical Device { ID: \"" << std::hex 
          << props.properties.deviceID << "\", Name: \""
          << props.properties.deviceName << "\" }" << std::dec << std::endl;

  // supported queue families
  std::vector<VkQueueFamilyProperties2> queueFamilyProps;
  queueFamilyProps.reserve(16);
  uint32_t queueFamilyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice, &queueFamilyCount, nullptr);
  queueFamilyProps.resize(queueFamilyCount);
  for (VkQueueFamilyProperties2& q : queueFamilyProps) {
    q = {};
    q.sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
    // TODO: performance query counters
  }
  vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice, &queueFamilyCount, queueFamilyProps.data());
  // TODO: OS specific?
  // - graphics (on apple and android, implicit presentation support)
  result.graphicsQueueFamilyIndex = [&]() {
    auto const it = std::find_if(queueFamilyProps.cbegin(), queueFamilyProps.cend(), [](VkQueueFamilyProperties2 const& q) {
      return q.queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT;
    });
    if (it == queueFamilyProps.cend()) return std::numeric_limits<uint32_t>::max();
    return static_cast<uint32_t>(std::distance(it, queueFamilyProps.cbegin()));
  }();
  if (result.graphicsQueueFamilyIndex == std::numeric_limits<uint32_t>::max())
    return result;

  result.computeQueueFamilyIndex = [&]() {
    if (props.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
      auto const it = std::find_if(queueFamilyProps.cbegin(), queueFamilyProps.cend(), [](VkQueueFamilyProperties2 const& q) {
        return !(q.queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                (q.queueFamilyProperties.queueFlags & VK_QUEUE_COMPUTE_BIT);
      });
      if (it != queueFamilyProps.cend())
        return static_cast<uint32_t>(std::distance(it, queueFamilyProps.cbegin()));
    }
    auto const it = std::find_if(queueFamilyProps.cbegin(), queueFamilyProps.cend(), [](VkQueueFamilyProperties2 const& q) {
      return q.queueFamilyProperties.queueFlags & VK_QUEUE_COMPUTE_BIT;
    });
    if (it == queueFamilyProps.cend()) return std::numeric_limits<uint32_t>::max();
    return static_cast<uint32_t>(std::distance(it, queueFamilyProps.cbegin()));
  }();
  if (result.computeQueueFamilyIndex == std::numeric_limits<uint32_t>::max())
    return result;

  result.transferQueueFamilyIndex = [&]() {
    if (props.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
      auto it = std::find_if(queueFamilyProps.cbegin(), queueFamilyProps.cend(), [](VkQueueFamilyProperties2 const& q) {
        return !(q.queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
               !(q.queueFamilyProperties.queueFlags & VK_QUEUE_COMPUTE_BIT) &&
                (q.queueFamilyProperties.queueFlags & VK_QUEUE_TRANSFER_BIT);
      });
      if (it != queueFamilyProps.cend())
        return static_cast<uint32_t>(std::distance(it, queueFamilyProps.cbegin()));
      it = std::find_if(queueFamilyProps.cbegin(), queueFamilyProps.cend(), [](VkQueueFamilyProperties2 const& q) {
        return !(q.queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                (q.queueFamilyProperties.queueFlags & VK_QUEUE_TRANSFER_BIT);
      });
      if (it != queueFamilyProps.cend())
        return static_cast<uint32_t>(std::distance(it, queueFamilyProps.cbegin()));
    }
    auto const it = std::find_if(queueFamilyProps.cbegin(), queueFamilyProps.cend(), [](VkQueueFamilyProperties2 const& q) {
      return q.queueFamilyProperties.queueFlags & VK_QUEUE_TRANSFER_BIT;
    });
    if (it == queueFamilyProps.cend()) return std::numeric_limits<uint32_t>::max();
    return static_cast<uint32_t>(std::distance(it, queueFamilyProps.cbegin()));
  }();
  if (result.transferQueueFamilyIndex == std::numeric_limits<uint32_t>::max())
    return result;

  // device extensions
  uint32_t devExtCount = 0;
  std::vector<VkExtensionProperties> devExtProps;
  devExtProps.reserve(256);
  AVK_VK_RST(vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &devExtCount, nullptr));
  devExtProps.resize(devExtCount);
  AVK_VK_RST(vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &devExtCount, devExtProps.data()));

  std::vector<char const*> requiredExtensions = getVulkanMinimalRequiredDeviceExtensions();
  std::vector<char const*> optionalExtensions = getVulkanOptionalDeviceExtensions();
  for (VkExtensionProperties const& ext : devExtProps) {
    auto const strCompareExtensions = [&ext](char const* name){ return strcmp(name, ext.extensionName) == 0; };
    auto it = std::find_if(requiredExtensions.begin(), requiredExtensions.end(), strCompareExtensions);
    if (it != requiredExtensions.end()) {
      requiredExtensions.erase(it);
    }

    auto optIt = std::find_if(optionalExtensions.begin(), optionalExtensions.end(), strCompareExtensions);
    if (optIt != optionalExtensions.end()) {
      if (strcmp(*optIt, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME) == 0) {
        result.optionalExtensions |= EVulkanOptionalExtensionSupport::MemoryBudget;
        theScore += 100;
      } else if (strcmp(*optIt, VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME) == 0) {
        result.optionalExtensions |= EVulkanOptionalExtensionSupport::DedicatedAllocation;
        theScore += 100;
      }
      optionalExtensions.erase(optIt);
    }

    if (requiredExtensions.empty() && optionalExtensions.empty())
      break;
  }
  if (!requiredExtensions.empty()) {
    LOG_ERR << "Unsupported Required Extensions:\n";
    for (char const* name : requiredExtensions) {
      LOG_ERR << "  - " << name << '\n';
    }
    LOG_ERR << LOG_RST << std::flush;
    return result;
  }

  // device features
  // compute positive score if still alive
  // TODO Future:
  // - (1.1) shader draw parameters, (ext)buffer device address, (G) swapchain maintenance
  // - attribute divisor (instancing)
  // - timelineSemaphore, uniformBufferStandardLayout, vulkanMemoryModel
  VkPhysicalDeviceFeatures2 features{};
  features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  VkPhysicalDeviceTimelineSemaphoreFeatures timelineFeatures{};
  timelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
  VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures{};
  bufferDeviceAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
  VkPhysicalDeviceUniformBufferStandardLayoutFeatures uniformBufferStandardLayoutFeatures{};
  uniformBufferStandardLayoutFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES;
  VkPhysicalDeviceVulkanMemoryModelFeatures vulkanMemoryModelFeatures{};
  vulkanMemoryModelFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES;
  VkPhysicalDevicePortabilitySubsetFeaturesKHR portabilitySubsetFeatures{};
  portabilitySubsetFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR;

  vulkanMemoryModelFeatures.pNext = &portabilitySubsetFeatures;
  uniformBufferStandardLayoutFeatures.pNext = &vulkanMemoryModelFeatures;
  bufferDeviceAddressFeatures.pNext = &uniformBufferStandardLayoutFeatures;
  timelineFeatures.pNext = &bufferDeviceAddressFeatures;
  features.pNext = &timelineFeatures;

  vkGetPhysicalDeviceFeatures2(physicalDevice, &features);
  if (!handleRequiredDeviceFeatures(features, true)) 
    return result;

  result.score = theScore;

  return result;
}

}

