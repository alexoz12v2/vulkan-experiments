#pragma once

#include "avkex-macros.h" // required before volk
#include "avkex-utils.h"

#include <volk.h>
#include <vk_mem_alloc.h> // must come after volk

#include <spirv_reflect.h>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace avkex {

enum class EVulkanOptionalExtensionSupport : uint64_t {
  MemoryBudget = static_cast<uint64_t>(1) << 0,
  DedicatedAllocation = static_cast<uint64_t>(1) << 1,
};
using VulkanExtBits = std::underlying_type_t<EVulkanOptionalExtensionSupport>;

constexpr inline VulkanExtBits operator&(
 EVulkanOptionalExtensionSupport a, EVulkanOptionalExtensionSupport b) {
  return static_cast<VulkanExtBits>(a) & static_cast<VulkanExtBits>(b); 
}

constexpr inline EVulkanOptionalExtensionSupport operator|(
 EVulkanOptionalExtensionSupport a, EVulkanOptionalExtensionSupport b) {
  return static_cast<EVulkanOptionalExtensionSupport>(
    static_cast<VulkanExtBits>(a) | static_cast<VulkanExtBits>(b));
}

constexpr inline EVulkanOptionalExtensionSupport& operator|=(
 EVulkanOptionalExtensionSupport& a, EVulkanOptionalExtensionSupport b) {
  a = a | b;
  return a;
}

struct VulkanPhysicalDeviceQueryResult {
  operator bool() const { return score > 0; }

  bool hasMemoryBudgetExt() const { return optionalExtensions & EVulkanOptionalExtensionSupport::MemoryBudget; }
  bool hasDedicatedAllocationExt() const { return optionalExtensions & EVulkanOptionalExtensionSupport::DedicatedAllocation; }

  EVulkanOptionalExtensionSupport optionalExtensions;
  // TODO can be modified in future for surface support on linux and windows
  uint32_t graphicsQueueFamilyIndex; 
  // if device is discrete (and you are not on apple or android), try to find
  // an async compute queue family (non graphics).
  uint32_t computeQueueFamilyIndex;
  // if device is discrete (and you are not on apple or android), try to find
  // a transfer only queue family
  uint32_t transferQueueFamilyIndex;
  int32_t score;
};

struct VulkanDeviceInfo {
  VkPhysicalDevice physicalDevice;
  VulkanPhysicalDeviceQueryResult queryResult;
};

std::vector<char const*> getVulkanMinimalRequiredDeviceExtensions();
std::vector<char const*> getVulkanOptionalDeviceExtensions();
bool handleRequiredDeviceFeatures(VkPhysicalDeviceFeatures2& features, bool checkMode);

// implicitly singleton. If you try to instantiate multiple ones, it will silently fail.
class VulkanApp {
 public:
  VulkanApp();
  VulkanApp(VulkanApp const&) = delete;
  VulkanApp(VulkanApp &&) noexcept = delete;
  VulkanApp& operator=(VulkanApp const&) = delete;
  VulkanApp& operator=(VulkanApp &&) noexcept = delete;
  ~VulkanApp() noexcept;

  static const std::function<VulkanPhysicalDeviceQueryResult(VkInstance,VkPhysicalDevice)> s_defaultDeviceValidator;

  operator bool() const { return m_instance; }
  VkInstance instance() const { return m_instance; }

  std::vector<VulkanDeviceInfo> getEligibleDevices(bool sorted = true, std::function<VulkanPhysicalDeviceQueryResult(VkInstance,VkPhysicalDevice)> const& devValidator = s_defaultDeviceValidator) const;

 private:
  VkInstance m_instance = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT m_messenger = VK_NULL_HANDLE;
};

// TODO: Remember to call vmaSetFrameIndex when starting to render a new frame
class VulkanDevice {
 public:
  VulkanDevice(VkInstance instance, VulkanDeviceInfo const& devInfo);
  VulkanDevice(VulkanDevice const&) = delete;
  VulkanDevice(VulkanDevice &&) noexcept;
  VulkanDevice& operator=(VulkanDevice const&) = delete;
  VulkanDevice& operator=(VulkanDevice &&) noexcept;
  ~VulkanDevice() noexcept;

  operator bool() const { return m_device; }
  VkPhysicalDevice physicalDevice() const { return m_physicalDevice; }
  VkDevice device() const { return m_device; }
  VolkDeviceTable const* api() const { return m_table.get(); }
  VmaAllocator allocator() const { return m_allocator; }

  VkQueue graphicsQueue() const { return m_graphicsQueue; }
  uint32_t graphicsQueueFamilyIndex() const { return m_graphicsQueueFamilyIndex; }
  VkSemaphore graphicsTimelineSemaphore() const { return m_graphicsTimelineSemaphore; }
  VkQueue computeQueue() const { return m_computeQueue; }
  uint32_t computeQueueFamilyIndex() const { return m_computeQueueFamilyIndex; }
  VkSemaphore computeTimelineSemaphore() const { return m_computeTimelineSemaphore; }
  VkQueue transferQueue() const { return m_transferQueue; }
  uint32_t transferQueueFamilyIndex() const { return m_transferQueueFamilyIndex; }

  void acquire();
  void release();

 private:
  VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
  VkDevice m_device = VK_NULL_HANDLE;
  std::unique_ptr<VolkDeviceTable> m_table;
  VmaAllocator m_allocator = VK_NULL_HANDLE;

  // queues (TODO more generic? maybe?)
  VkQueue m_graphicsQueue = VK_NULL_HANDLE;
  uint32_t m_graphicsQueueFamilyIndex = -1U;
  VkSemaphore m_graphicsTimelineSemaphore = VK_NULL_HANDLE;
  VkQueue m_computeQueue = VK_NULL_HANDLE;
  uint32_t m_computeQueueFamilyIndex = -1U;
  VkSemaphore m_computeTimelineSemaphore = VK_NULL_HANDLE;
  VkQueue m_transferQueue = VK_NULL_HANDLE;
  uint32_t m_transferQueueFamilyIndex = -1U;

  // Dependency Injection management
  std::atomic_int m_refCount = 0;
  static_assert(std::atomic_int::is_always_lock_free); // for .exchange
};

// Discard Pool Based Resource Management
// - Warning: assumes VkSemaphore last enough
class VulkanDiscardPoolImpl;
class VulkanDiscardPool {
 public:
  VulkanDiscardPool(VulkanDevice* dev);
  VulkanDiscardPool(VulkanDiscardPool const&) = delete;
  VulkanDiscardPool(VulkanDiscardPool &&) noexcept = delete;
  VulkanDiscardPool& operator=(VulkanDiscardPool const&) = delete;
  VulkanDiscardPool& operator=(VulkanDiscardPool &&) noexcept = delete;
  ~VulkanDiscardPool() noexcept;

  bool registerTimelineSemaphore(VkSemaphore sem, uint32_t minResCap = 256, uint32_t compactionThreshold = 1024, uint32_t maxResCap = 2048);
  bool unregisterTimelineSemaphore(VkSemaphore sem);

  void collect();
  void collectSemaphore(VkSemaphore sem);

  bool discardBuffer(VkSemaphore sem, uint64_t readyValue, VkBuffer buffer, VmaAllocation alloc);
  bool discardImage(VkSemaphore sem, uint64_t readyValue, VkImage image, VmaAllocation alloc);
  // ...

 private:
  VulkanDevice* m_dev = nullptr;
  std::unique_ptr<VulkanDiscardPoolImpl> m_impl = nullptr;
};

// Command Buffer Management
// - 1 command pool buffer per thread. (Dynamic TLS or heap)
// - command pool should allow resettable command buffers
// - when a command pool is empty, create a new one. (no trim)
// - when can an exhausted command pool be reset? Never, reuse command buffers?
//   - assuming we have a global timeline semaphore, whose value starts at 0 and
//     increments by one each queue submission (1 for rendering and 1 for compute)
//   - command buffer can be associated with the timeline value on which it should
//     be put out of the pending state
// - should expose a method to explicitly trim the command buffer manager,
//   - deallocate excess command pools, which are identified by the fact that
//     their allocation count is zero
//   - trim last pool of the given thread
// - copy control
//   - no copy, no move
//   - destructor: only 1 thread should enter destructor. Lock the storage for all
//     threads, and wait for all command buffers which are pending to be done. Then
//     release all command pools
// WARNING: When creating new resources, you can use the "target" semaphore value,
//    but when retrieving, we need to use the "actual" semaphore value
class VulkanCommandBufferManagerImpl;
class VulkanCommandBufferManager {
 public:
  VulkanCommandBufferManager(VulkanDevice* dev);
  VulkanCommandBufferManager(VulkanCommandBufferManager const&) = delete;
  VulkanCommandBufferManager(VulkanCommandBufferManager &&) noexcept = delete;
  VulkanCommandBufferManager& operator=(VulkanCommandBufferManager const&) = delete;
  VulkanCommandBufferManager& operator=(VulkanCommandBufferManager &&) noexcept = delete;
  ~VulkanCommandBufferManager() noexcept;

  VkCommandBuffer getThreadLocalComputeCommandBufferForTimeline(uint64_t timelineValue);
  VkCommandBuffer getThreadLocalGraphicsCommandBufferForTimeline(uint64_t timelineValue);

 private:
  VulkanDevice* m_dev = nullptr;
  std::unique_ptr<VulkanCommandBufferManagerImpl> m_impl = nullptr;
};

// Compute Shader Management
// - https://docs.vulkan.org/spec/latest/chapters/shaders.html#shaders-compute
// - (Vulkan) global workgroup = (CUDA) Grid,
// - (Vulkan) local workgroup = (CUDA) Block -> shared memory and ex/mem barrier
// - warp/wave -> (vulkan) subgroup, needed SPIR-V Capability SPV_KHR_subgroup_*
//   - https://developer.nvidia.com/blog/reading-between-the-threads-shader-intrinsics/
// - https://raphlinus.github.io/gpu/2020/04/30/prefix-sum.html
//   - `gl_SubgroupSize` variable is defined to have the value from VkPhysicalDeviceSubgroupProperties
// - https://www.khronos.org/blog/vulkan-subgroup-tutorial
// - (SPIRV Cap) SPV_KHR_non_semantic_info -> (VK ext) VK_KHR_shader_non_semantic_info (promoted to 1.3)
//   - allows for printf (debugPrintfEXT) inside shader. https://docs.vulkan.org/samples/latest/samples/extensions/shader_debugprintf/README.html
//   - Using debug printf will consume a descriptor set, so if you use every last descriptor set it may not work and you may need to increase the set count at pool allocation.
// - The WorkgroupSize was deprecated starting with version 1.6 in favor of using LocalSizeId. The main issue is Vulkan doesn't support LocalSizeId unless you have VK_KHR_maintenance4 or Vulkan 1.3+
struct VulkanDescriptorSetLayoutData {
  VkDescriptorSetLayoutCreateInfo createInfo;
  uint32_t setNumber;
  std::vector<VkDescriptorSetLayoutBinding> bindings;
};
std::vector<VulkanDescriptorSetLayoutData> reflectShaderDescriptors(SpvReflectShaderModule const& spvShaderModule);
VkDescriptorSetLayout createDescriptorSetLayout(VulkanDevice& dev, VulkanDescriptorSetLayoutData const& setLayoutData);

class VulkanShaderRegistryImpl;
class VulkanShaderRegistry {
 public:
  // makes a copy
  VulkanShaderRegistry(VulkanDevice* dev, size_t minCap = 64, size_t maxCap = 2048);
  VulkanShaderRegistry(VulkanShaderRegistry const&) = delete;
  VulkanShaderRegistry(VulkanShaderRegistry &&) noexcept = delete;
  VulkanShaderRegistry& operator=(VulkanShaderRegistry const&) = delete;
  VulkanShaderRegistry& operator=(VulkanShaderRegistry &&) noexcept = delete;
  ~VulkanShaderRegistry() noexcept;

  bool registerShader(std::string_view name, uint32_t const* pCode, uint32_t wordCount);
  bool unregisterShader(std::string_view name);
  bool withShader(std::string_view shader, std::function<void(VkShaderModule, SpvReflectShaderModule const&)> const& func) const;

  bool registerShader(std::string_view name, std::vector<uint32_t>&& code) { return code.empty() ? false : registerShader(name, code.data(), static_cast<uint32_t>(code.size())); }

 private:
  VulkanDevice* m_dev = nullptr;
  std::unique_ptr<VulkanShaderRegistryImpl> m_impl;
};

// Basic compute pipeline creation
// - while spvShaderModule._internal->code_size exists, I prefer not relying on internal behaviour
// - TODO: throw away reflection data once VkShaderModule is created
VkPipelineLayout createPipelineLayout(VulkanDevice& dev, uint32_t setLayoutCount = 0, VkDescriptorSetLayout const* pSetLayouts = nullptr, uint32_t pushConstantRangeCount = 0, VkPushConstantRange const* pPushConstantRanges = nullptr);
VkPipeline createComputePipeline(VulkanDevice& dev, VkPipelineLayout pipelineLayout, SpvReflectShaderModule const& spvShaderModule, VkShaderModule shaderModule);

// Memory Management with VMA

}


