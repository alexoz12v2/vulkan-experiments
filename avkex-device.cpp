#include "avkex.h"

#include <cassert>
#include <utility>
#include <set>
#include <thread>

using namespace avkex;

namespace {

void cleanupDevice(VulkanDevice& device);
void waitForZero(std::atomic_int& counter);
VmaAllocator createVmaAllocator(VkInstance instance, VkDevice device, VkPhysicalDevice physicalDevice, EVulkanOptionalExtensionSupport optionalExtensions, uint32_t vulkanApiVersion = VK_API_VERSION_1_1);

}

namespace avkex {

VulkanDevice::VulkanDevice(VulkanDevice&& that) noexcept 
 : m_physicalDevice(std::exchange(that.m_physicalDevice, VK_NULL_HANDLE)),
   m_device(std::exchange(that.m_device, VK_NULL_HANDLE)),
   m_table(std::exchange(that.m_table, nullptr)),
   m_allocator(std::exchange(that.m_allocator, VK_NULL_HANDLE)),
   m_graphicsQueue(std::exchange(that.m_graphicsQueue, VK_NULL_HANDLE)),
   m_graphicsQueueFamilyIndex(std::exchange(that.m_graphicsQueueFamilyIndex, -1U)),
   m_graphicsTimelineSemaphore(std::exchange(that.m_graphicsTimelineSemaphore, VK_NULL_HANDLE)),
   m_computeQueue(std::exchange(that.m_computeQueue, VK_NULL_HANDLE)),
   m_computeQueueFamilyIndex(std::exchange(that.m_computeQueueFamilyIndex, -1U)),
   m_computeTimelineSemaphore(std::exchange(that.m_computeTimelineSemaphore, VK_NULL_HANDLE)),
   m_transferQueue(std::exchange(that.m_transferQueue, VK_NULL_HANDLE)),
   m_transferQueueFamilyIndex(std::exchange(that.m_transferQueueFamilyIndex, -1U)) {

  waitForZero(that.m_refCount);
}

VulkanDevice& VulkanDevice::operator=(VulkanDevice&& that) noexcept {
  if (this != &that) {
    waitForZero(m_refCount);
    cleanupDevice(*this);
    m_table.release();

    waitForZero(that.m_refCount);

    m_physicalDevice = std::exchange(that.m_physicalDevice, VK_NULL_HANDLE);
    m_device = std::exchange(that.m_device, VK_NULL_HANDLE);
    m_table = std::exchange(that.m_table, nullptr);
    m_allocator = std::exchange(that.m_allocator, nullptr);
    m_graphicsQueue = std::exchange(that.m_graphicsQueue, VK_NULL_HANDLE);
    m_graphicsQueueFamilyIndex = std::exchange(that.m_graphicsQueueFamilyIndex, -1U);
    m_graphicsTimelineSemaphore = std::exchange(that.m_graphicsTimelineSemaphore, VK_NULL_HANDLE);
    m_computeQueue = std::exchange(that.m_computeQueue, VK_NULL_HANDLE);
    m_computeQueueFamilyIndex = std::exchange(that.m_computeQueueFamilyIndex, -1U);
    m_computeTimelineSemaphore = std::exchange(that.m_computeTimelineSemaphore, VK_NULL_HANDLE);
    m_transferQueue = std::exchange(that.m_transferQueue, VK_NULL_HANDLE);
    m_transferQueueFamilyIndex = std::exchange(that.m_transferQueueFamilyIndex, -1U);
    m_refCount.store(that.m_refCount.exchange(0, std::memory_order_relaxed), std::memory_order_relaxed);
  }
  return *this;
}

void VulkanDevice::acquire() {
  m_refCount.fetch_add(1, std::memory_order_relaxed);
}

void VulkanDevice::release() { 
  int const previous = m_refCount.fetch_sub(1, std::memory_order_relaxed);
  assert(previous >= 0);
}

VulkanDevice::VulkanDevice(VkInstance instance, VulkanDeviceInfo const& devInfo) 
 : m_physicalDevice(devInfo.physicalDevice), m_table(std::make_unique<VolkDeviceTable>()) {
  // features
  VkPhysicalDeviceFeatures2 features{};
  features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  VkPhysicalDeviceTimelineSemaphoreFeatures timelineSemaphoreFeatures{};
  timelineSemaphoreFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
  VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures{};
  bufferDeviceAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
  VkPhysicalDeviceUniformBufferStandardLayoutFeatures uniformBufferStandardLayoutFeatures{};
  uniformBufferStandardLayoutFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES;
  VkPhysicalDeviceVulkanMemoryModelFeatures vulkanMemoryModelFeatures{};
  vulkanMemoryModelFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES;
  VkPhysicalDevicePortabilitySubsetFeaturesKHR portabilitySubsetFeatures{};
  portabilitySubsetFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR;

  uniformBufferStandardLayoutFeatures.pNext = &portabilitySubsetFeatures;
  bufferDeviceAddressFeatures.pNext = &uniformBufferStandardLayoutFeatures;
  timelineSemaphoreFeatures.pNext = &bufferDeviceAddressFeatures;
  vulkanMemoryModelFeatures.pNext = &timelineSemaphoreFeatures;
  features.pNext = &vulkanMemoryModelFeatures;
  handleRequiredDeviceFeatures(features, false);

  // extensions
  std::vector<char const*> extensions = getVulkanMinimalRequiredDeviceExtensions();
  if (devInfo.queryResult.hasMemoryBudgetExt()) {
    extensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
  }
  if (devInfo.queryResult.hasDedicatedAllocationExt()) {
    extensions.push_back(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME);
  }

  // queues (TODO more generic? maybe?)
  float queuePriority = 1.f;
  std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
  std::set<uint32_t> const uniqueFamilyIndices {
    devInfo.queryResult.graphicsQueueFamilyIndex, 
    devInfo.queryResult.computeQueueFamilyIndex,
    devInfo.queryResult.transferQueueFamilyIndex
  };

  queueCreateInfos.resize(uniqueFamilyIndices.size());
  uint32_t index = 0;
  for (uint32_t family : uniqueFamilyIndices) {
    uint32_t const i = index++;
    queueCreateInfos[i] = {};
    queueCreateInfos[i].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfos[i].queueFamilyIndex = family;
    queueCreateInfos[i].queueCount = 1; // TODO?
    queueCreateInfos[i].pQueuePriorities = &queuePriority;
  }

  VkDeviceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  createInfo.pNext = &features;
  createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
  createInfo.pQueueCreateInfos = queueCreateInfos.data();
  createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  createInfo.ppEnabledExtensionNames = extensions.data();

  // note: using "VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT" (printf in shaders)
  //  implicitly activates, with warnings, the following features/device extensions (on Apple M4)
  //  - fragmentStoresAndAtomics, vertexPipelineStoresAndAtomics, shaderInt64, VkPhysicalDeviceVulkanMemoryModelFeatures::vulkanMemoryModelDeviceScope, VkPhysicalDeviceScalarBlockLayoutFeatures::scalarBlockLayout, VkPhysicalDevice8BitStorageFeatures::storageBuffer8BitAccess,
  AVK_VK_RST(vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device));
  volkLoadDeviceTable(m_table.get(), m_device);

  // fetch queues
  VkDeviceQueueInfo2 queueInfo{};
  queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2;
  queueInfo.queueFamilyIndex = devInfo.queryResult.graphicsQueueFamilyIndex;
  m_table->vkGetDeviceQueue2(m_device, &queueInfo, &m_graphicsQueue);
  queueInfo.queueFamilyIndex = devInfo.queryResult.computeQueueFamilyIndex;
  m_table->vkGetDeviceQueue2(m_device, &queueInfo, &m_computeQueue);
  queueInfo.queueFamilyIndex = devInfo.queryResult.transferQueueFamilyIndex;
  m_table->vkGetDeviceQueue2(m_device, &queueInfo, &m_transferQueue);

  // timeline semaphores (TODO: VkExportSemaphoreCreateInfo)
  VkSemaphoreCreateInfo semCreateInfo{};
  semCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  VkSemaphoreTypeCreateInfo semTypeCreateInfo{};
  semTypeCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
  semTypeCreateInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
  semTypeCreateInfo.initialValue = 0;
  semCreateInfo.pNext = &semTypeCreateInfo; 
  AVK_VK_RST(m_table->vkCreateSemaphore(m_device, &semCreateInfo, nullptr, &m_graphicsTimelineSemaphore));
  AVK_VK_RST(m_table->vkCreateSemaphore(m_device, &semCreateInfo, nullptr, &m_computeTimelineSemaphore));

  // queue family indices assignment
  m_graphicsQueueFamilyIndex = devInfo.queryResult.graphicsQueueFamilyIndex;
  m_computeQueueFamilyIndex = devInfo.queryResult.computeQueueFamilyIndex;
  m_transferQueueFamilyIndex = devInfo.queryResult.transferQueueFamilyIndex;

  // Create VMA Allocator
  m_allocator = createVmaAllocator(instance, m_device, m_physicalDevice, devInfo.queryResult.optionalExtensions);
}

VulkanDevice::~VulkanDevice() noexcept {
  waitForZero(m_refCount);
  cleanupDevice(*this);
}

}

namespace {

void cleanupDevice(VulkanDevice& device) {
  if (auto* api = device.api(); api && device.device()) {
    VkDevice dev = device.device(); 
    api->vkDeviceWaitIdle(dev); 

    if (VmaAllocator allocator = device.allocator(); allocator != VK_NULL_HANDLE) {
      vmaDestroyAllocator(allocator);
    }

    api->vkDestroySemaphore(dev, device.graphicsTimelineSemaphore(), nullptr);
    api->vkDestroySemaphore(dev, device.computeTimelineSemaphore(), nullptr);
    api->vkDestroyDevice(device.device(), nullptr);
  }
}

void waitForZero(std::atomic_int& counter) {
  while (counter.load(std::memory_order_relaxed)) {
    std::this_thread::yield();
  }
}

VmaAllocator createVmaAllocator(
 VkInstance instance, VkDevice device, VkPhysicalDevice physicalDevice, 
 EVulkanOptionalExtensionSupport optionalExtensions, uint32_t vulkanApiVersion) {
  VmaAllocatorCreateInfo allocatorCreateInfo{};
  allocatorCreateInfo.physicalDevice = physicalDevice;
  allocatorCreateInfo.device = device;
  allocatorCreateInfo.instance = instance;
  allocatorCreateInfo.vulkanApiVersion = vulkanApiVersion;

  // no internal mutexes, we'll sync allocations ourselves
  allocatorCreateInfo.flags |= VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT;

  // buffer device address is a required extension. allows usage VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT. VkMemory backing it will have VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT automatically added by the library
  allocatorCreateInfo.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

  // maintenance4 is a required extension
  allocatorCreateInfo.flags |= VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE4_BIT;

  if (optionalExtensions & EVulkanOptionalExtensionSupport::MemoryBudget)
    allocatorCreateInfo.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
  if (optionalExtensions & EVulkanOptionalExtensionSupport::DedicatedAllocation)
    allocatorCreateInfo.flags |= VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT;

#ifdef _WIN32
  allocatorCreateInfo.flags |= VMA_ALLOCATOR_CREATE_KHR_EXTERNAL_MEMORY_WIN32_BIT;
#endif

  VmaVulkanFunctions vulkanFunctions{};
  AVK_VK_RST(vmaImportVulkanFunctionsFromVolk(&allocatorCreateInfo, &vulkanFunctions));
  allocatorCreateInfo.pVulkanFunctions = &vulkanFunctions;

  VmaAllocator allocator = VK_NULL_HANDLE;
  AVK_VK_RST(vmaCreateAllocator(&allocatorCreateInfo, &allocator));
  return allocator;
}

}

