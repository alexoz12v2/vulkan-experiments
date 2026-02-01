#include "avkex.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <iostream>
#include <shared_mutex>
#include <thread>
#include <unordered_map>

using namespace avkex;

namespace avkex {

// ------------------------------------------------------------------------------
// VulkanCommandBufferManagerImpl
// ------------------------------------------------------------------------------

struct BufferTimelinePair {
  VkCommandBuffer commandBuffer;
  uint64_t timelineValue;
};

// this is effectively thread local
struct PoolBuffersPair {
  VkCommandPool commandPool;
  std::vector<BufferTimelinePair> graphicsBuffers;
  std::vector<BufferTimelinePair> computeBuffers;
};

// TODO: add handling of secondary command buffers
class VulkanCommandBufferManagerImpl {
  static size_t constexpr POOLS_CAPACITY = 4;
  static size_t constexpr BUFFERS_CAPACITY = 64;
  enum class EQueueType : uint32_t { Graphics, Compute };
 public:
  // find first command buffer whose timeline value is strictly less than.
  // if not found or full, allocate a new pool. Any unexpected failure return null
  VkCommandBuffer tryGetThreadLocalGraphicsCommandBufferAtTimeline(VulkanDevice& dev, uint64_t timelineValue);
  VkCommandBuffer tryGetThreadLocalComputeCommandBufferAtTimeline(VulkanDevice& dev, uint64_t timelineValue);

  // should be called at destruction, hence all timelines should be done. We won't
  // wait for them here
  void cleanup(VulkanDevice& dev);

 private:
  // core internal logic for both queue types
  VkCommandBuffer getCommandBufferInternal(VulkanDevice& dev, uint64_t timelineValue, EQueueType queueType);

  // helpers to reduce code duplication
  VkResult createPool(VulkanDevice& dev, uint32_t queueFamilyIndex, VkCommandPool* outPool);
  VkResult allocateBuffer(VulkanDevice& dev, VkCommandPool pool, VkCommandBuffer* outBuffer);

  // helper to get or create "thread-local storage"
  std::vector<PoolBuffersPair>* getThreadLocalPools(VulkanDevice& dev);

  std::unordered_map<std::thread::id, std::vector<PoolBuffersPair>> m_map;
  std::shared_mutex m_mapMtx;
};

VkCommandBuffer VulkanCommandBufferManagerImpl::tryGetThreadLocalGraphicsCommandBufferAtTimeline(VulkanDevice& dev, uint64_t timelineValue) {
  return getCommandBufferInternal(dev, timelineValue, EQueueType::Graphics);
}

VkCommandBuffer VulkanCommandBufferManagerImpl::tryGetThreadLocalComputeCommandBufferAtTimeline(VulkanDevice& dev, uint64_t timelineValue) {
  return getCommandBufferInternal(dev, timelineValue, EQueueType::Compute);
}

void VulkanCommandBufferManagerImpl::cleanup(VulkanDevice& dev) {
  std::lock_guard lock{m_mapMtx};
  for (auto& [tid, pools] : m_map) {
    for (auto& pair : pools) {
      dev.api()->vkDestroyCommandPool(dev.device(), pair.commandPool, nullptr);
    }
  }
}

VkCommandBuffer VulkanCommandBufferManagerImpl::getCommandBufferInternal(VulkanDevice& dev, uint64_t timelineValue, EQueueType queueType) {
  auto* vkApi = dev.api();
  VkDevice device = dev.device();

  // Determine Queue Specifics
  uint32_t queueFamilyIndex = (queueType == EQueueType::Graphics) ? dev.graphicsQueueFamilyIndex() : dev.computeQueueFamilyIndex();
  VkSemaphore timelineSemaphore = (queueType == EQueueType::Compute) ? dev.graphicsTimelineSemaphore() : dev.computeTimelineSemaphore();

  // get thread local storage
  auto* poolsVector = getThreadLocalPools(dev);
  if (!poolsVector) return VK_NULL_HANDLE;

  // get current GPU progress
  uint64_t actualTimeline = 0;
  // since we chose a Vulkan1.1 instance, the populated function is the KHR one
  AVK_VK_RST(vkApi->vkGetSemaphoreCounterValueKHR(device, timelineSemaphore, &actualTimeline));

  // 1. Try to find a reusable buffer in existing pools
  for (auto& poolPair : *poolsVector) {
    // select the correct vector based on queue type
    auto& bufferList = (queueType == EQueueType::Graphics) ? poolPair.graphicsBuffers : poolPair.computeBuffers;

    // find a completed buffer (not pending) (TODO: something better than linear scan?)
    for (auto& bufPair : bufferList) { 
      if (bufPair.timelineValue < actualTimeline) {
        // Buffer is ready for reuse
        vkApi->vkResetCommandBuffer(bufPair.commandBuffer, 0);
        bufPair.timelineValue = timelineValue;

        return bufPair.commandBuffer;
      }
    }

    // 2. If no reusable buffer found, check if we can allocate a new one in this pool
    if (bufferList.size() < BUFFERS_CAPACITY) {
      VkCommandBuffer newBuf = VK_NULL_HANDLE;
      VkResult const res = allocateBuffer(dev, poolPair.commandPool, &newBuf);
      if (res == VK_SUCCESS) {
        bufferList.push_back({newBuf, timelineValue});
        return newBuf;
      } else if (res == VK_ERROR_OUT_OF_DEVICE_MEMORY || res == VK_ERROR_OUT_OF_HOST_MEMORY) {
        // if this specific pool is full/fragmented, continue to the next pool. TODO: Log
        continue;
      } else {
        AVK_VK_RST(res); // unexpected error
        return VK_NULL_HANDLE;
      }
    }
  }

  // 3. All existing pools are full or exhausted. Create a new pool?
  if (poolsVector->size() < POOLS_CAPACITY) {
    poolsVector->emplace_back();
    auto& newPair = poolsVector->back();

    // reserve vectors
    newPair.graphicsBuffers.reserve(BUFFERS_CAPACITY);
    newPair.computeBuffers.reserve(BUFFERS_CAPACITY);

    VkResult res = createPool(dev, queueFamilyIndex, &newPair.commandPool);
    if (res != VK_SUCCESS) {
      poolsVector->pop_back(); // rollback
      return VK_NULL_HANDLE;
    }

    // allocate the new buffer
    VkCommandBuffer newBuf = VK_NULL_HANDLE;
    res = allocateBuffer(dev, newPair.commandPool, &newBuf);
    if (res == VK_SUCCESS) {
      auto& bufferList = (queueType == EQueueType::Graphics) ? newPair.graphicsBuffers : newPair.computeBuffers;
      bufferList.push_back({newBuf, timelineValue});
      return newBuf;
    } else {
      // cleanup empty pool if allocation failed
      vkApi->vkDestroyCommandPool(device, newPair.commandPool, nullptr);
      poolsVector->pop_back();
      return VK_NULL_HANDLE;
    }
  }

  // if we are here, we hit POOLS_CAPACITY and everything is busy.
  return VK_NULL_HANDLE;
}

VkResult VulkanCommandBufferManagerImpl::createPool(VulkanDevice& dev, uint32_t queueFamilyIndex, VkCommandPool* outPool) {
  VkCommandPoolCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  // RESET_COMMAND_BUFFER_BIT allows us to reset individual buffers rather than the whole pool
  createInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  createInfo.queueFamilyIndex = queueFamilyIndex;
  return dev.api()->vkCreateCommandPool(dev.device(), &createInfo, nullptr, outPool);
}

VkResult VulkanCommandBufferManagerImpl::allocateBuffer(VulkanDevice& dev, VkCommandPool pool, VkCommandBuffer* outBuffer) {
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = pool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1; // TODO?
  return dev.api()->vkAllocateCommandBuffers(dev.device(), &allocInfo, outBuffer);
}

std::vector<PoolBuffersPair>* VulkanCommandBufferManagerImpl::getThreadLocalPools(VulkanDevice& dev) {
  std::thread::id const tid = std::this_thread::get_id();
  // 1. Fast Path: Optimistic read lock
  {
    std::shared_lock rLock{m_mapMtx};
    auto it = m_map.find(tid);
    if (it != m_map.end()) {
      return &it->second;
    }
  }
  // 2. Slow Path: Write Lock to create entry
  std::lock_guard wLock{m_mapMtx};
  // double-check just in case another thread inserted wwhile we switched locks
  auto [it, inserted] = m_map.try_emplace(tid);
  // we reserve space to avoid reallocation, but we do NOT create the first pool
  // here. We let the main logic handle "empty list" same as "full list" to unify
  // code paths
  if (inserted) {
    it->second.reserve(POOLS_CAPACITY);
  }
  return &it->second;
}

// ------------------------------------------------------------------------------
// VulkanCommandBufferManager
// ------------------------------------------------------------------------------

VulkanCommandBufferManager::VulkanCommandBufferManager(VulkanDevice* dev) {
  assert(dev && *dev);
  dev->acquire();
  m_dev = dev;
  LOG_LOG << "VulkanCommandBufferManager acquired Device " << m_dev->device() << std::endl;

  m_impl = std::make_unique<VulkanCommandBufferManagerImpl>();
}

VkCommandBuffer VulkanCommandBufferManager::getThreadLocalComputeCommandBufferForTimeline(uint64_t timelineValue) {
  return m_impl->tryGetThreadLocalComputeCommandBufferAtTimeline(*m_dev, timelineValue);
}

VkCommandBuffer VulkanCommandBufferManager::getThreadLocalGraphicsCommandBufferForTimeline(uint64_t timelineValue) {
  return m_impl->tryGetThreadLocalGraphicsCommandBufferAtTimeline(*m_dev, timelineValue);
}

VulkanCommandBufferManager::~VulkanCommandBufferManager() noexcept {
  m_impl->cleanup(*m_dev);
  m_impl.release();
  m_dev->release();
  LOG_LOG << "VulkanCommandBufferManager released Device " << m_dev->device() << std::endl;
}

}

