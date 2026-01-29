#include "avkex.h"

#include <algorithm>
#include <atomic>
#include <iostream>
#include <thread>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

using namespace avkex;

struct PendingBuffer {
  PendingBuffer(VkBuffer _buffer, VmaAllocation _alloc, uint64_t _readyValue)
  : resource(_buffer), alloc(_alloc), readyValue(_readyValue) {}

  VkBuffer resource;
  VmaAllocation alloc;
  uint64_t readyValue;
};

struct PendingImage {
  PendingImage(VkImage _image, VmaAllocation _alloc, uint64_t _readyValue)
  : resource(_image), alloc(_alloc), readyValue(_readyValue) {}

  VkImage resource;
  VmaAllocation alloc;
  uint64_t readyValue;
};

namespace avkex {

// ------------------------------------------------------------------------------
// SemaphoreContent
// ------------------------------------------------------------------------------

// TODO: Setter for compaction threshold (manage latency spikes)
class SemaphoreContent {
 private:
  template <typename T>
  struct VectorOffset {
    explicit VectorOffset(uint32_t cap) : atomVec(cap), start(0) {}

    AtomicVector<T> atomVec;
    size_t start;
  };

 public:
  SemaphoreContent(uint32_t _minCap, uint compactionThreshold, uint32_t _maxCap) 
   : m_buffers(_minCap), m_images(_minCap), 
     m_maxCap(_maxCap), m_compactionThreshold(compactionThreshold) {
    assert(m_compactionThreshold <= m_maxCap);
  }
  SemaphoreContent(SemaphoreContent const&) = delete;
  SemaphoreContent(SemaphoreContent &&) noexcept = default;
  SemaphoreContent& operator=(SemaphoreContent const&) = delete;
  SemaphoreContent& operator=(SemaphoreContent &&) noexcept = default;


  // TODO: if atomics are mutable this can be made const
  bool allEmpty() {
    bool result = false;

    m_buffers.atomVec.readDo([&result](std::vector<PendingBuffer> const& vec) {
      result |= vec.empty();
    });
    if (result) return true;

    m_images.atomVec.readDo([&result](std::vector<PendingImage> const& vec) {
      result |= vec.empty();
    });
    if (result) return true;
    // ...

    return false;
  }

  void setCompactThreshold(uint32_t newVal) {
    m_compactionThreshold = newVal;
    assert(m_compactionThreshold <= m_maxCap);
  }

  // TODO: method recycle buffer, recyle image (need to store more metadata, different vectors)
  bool discardBuffer(VkBuffer buffer, VmaAllocation alloc, uint64_t readyValue) {
    return discardStuff<PendingBuffer>(m_buffers.atomVec, m_maxCap, buffer, alloc, readyValue);
  }
  bool discardImage(VkImage image, VmaAllocation alloc, uint64_t readyValue) {
    return discardStuff<PendingImage>(m_images.atomVec, m_maxCap, image, alloc, readyValue);
  }

  // TODO reuse?
  void collect(VulkanDevice& dev, uint64_t readyValue) {
    collectVec(m_images, readyValue, m_compactionThreshold, [&dev](VkImage image, VmaAllocation alloc) {
      vmaDestroyImage(dev.allocator(), image, alloc);
    });
    collectVec(m_buffers, readyValue, m_compactionThreshold, [&dev](VkBuffer buffer, VmaAllocation alloc) {
      vmaDestroyBuffer(dev.allocator(), buffer, alloc);
    });
    // ...
  }

 private:
  // Note: T&& is a Forwarding Reference only if you don't specify it.
  template <typename T, typename... Args> // TODO enable_if constructible_from
  static inline bool discardStuff(AtomicVector<T>& atomVec, uint32_t maxCap, Args&&... args) {
    std::vector<T>& vec = atomVec.acquireWrite();
    bool const result = vectorEmplaceWithGrowthLimit(vec, maxCap, std::forward<Args>(args)...);
    atomVec.releaseWrite();
    return result;
  }

  template <typename T, typename F> // TODO ensure it has "readyValue" member (std::declval)
  static inline void collectVec(VectorOffset<T>& v, uint64_t readyValue, uint32_t compactThreshold, F&& discard) {
    static_assert(
      std::is_standard_layout_v<T> 
      && std::is_same_v<decltype(std::declval<T>().readyValue), uint64_t>
      && sizeof(decltype(std::declval<T>().resource)) == 8); // size of a vulkan handle

    v.atomVec.writeDo([&discard, compactThreshold, readyValue, &start = v.start](std::vector<T>& pending) {
      assert(pending.empty() || pending.back().readyValue <= readyValue);
      // exploit the fact that timeline values are strictly increasing
      while (start < pending.size() && pending[start].readyValue <= readyValue) {
        if constexpr (std::is_same_v<decltype(std::declval<T>().alloc), VmaAllocation>) {
          discard(pending[start].resource, pending[start].alloc);
        } else {
          discard(pending[start].resource);
        }
        ++start;
      }

      if (start > compactThreshold) {
        pending.erase(pending.begin(), pending.begin() + start);
        start = 0;
      }
    });
  }

  // the buffers. Add more as needed (VkRenderPass, VkFramebuffer, ...)
  // are (VkDescriptorSet, VkDescriptorPool) needed?
  VectorOffset<PendingBuffer> m_buffers;
  VectorOffset<PendingImage> m_images;

  uint32_t m_maxCap;
  uint32_t m_compactionThreshold;
};

// ------------------------------------------------------------------------------
// VulkanDiscardPoolImpl
// ------------------------------------------------------------------------------

class VulkanDiscardPoolImpl {
 private:
  static uint32_t constexpr MAP_CAPACITY = 64;
 public:
  VulkanDiscardPoolImpl();
  void cleanup(VulkanDevice& dev) noexcept;

  bool registerTimelineSemaphore(VkSemaphore sem, uint32_t minCap, uint32_t threshold, uint32_t maxCap);
  bool unregisterTimelineSemaphore(VulkanDevice& dev, VkSemaphore sem);

  void collect(VulkanDevice& dev);
  void collectSemaphore(VulkanDevice& dev, VkSemaphore sem);

  bool discardBuffer(VkSemaphore sem, uint64_t readyValue, VkBuffer buffer, VmaAllocation alloc);
  bool discardImage(VkSemaphore sem, uint64_t readyValue, VkImage image, VmaAllocation alloc);
  // ...

 private:
  std::unordered_map<VkSemaphore, SemaphoreContent> m_map;
  std::shared_mutex m_mapMtx;
};

VulkanDiscardPoolImpl::VulkanDiscardPoolImpl() {
  m_map.reserve(MAP_CAPACITY);
}

void VulkanDiscardPoolImpl::cleanup(VulkanDevice& dev) noexcept {
  auto const checkMapEmpty = [this]() -> bool {
    std::shared_lock rLock{m_mapMtx};
    return m_map.empty();
  };
  while (!checkMapEmpty()) {
    unregisterTimelineSemaphore(dev, m_map.begin()->first);
  }
}

bool VulkanDiscardPoolImpl::registerTimelineSemaphore(VkSemaphore sem, uint32_t minCap, uint32_t threshold, uint32_t maxCap) {
  { // if already exists, do nothing and return false
    std::shared_lock rLock{m_mapMtx};
    if (m_map.find(sem) != m_map.cend())
      return false;
  }
  std::lock_guard wLock{m_mapMtx};
  // else try to insert and construct it
  auto [it, wasInserted] = m_map.try_emplace(sem, minCap, threshold, maxCap);
  return wasInserted;
}

bool VulkanDiscardPoolImpl::unregisterTimelineSemaphore(VulkanDevice& dev, VkSemaphore sem) {
  { // if it's not here, do nothing and return false
    std::shared_lock rLock{m_mapMtx};
    if (m_map.find(sem) == m_map.end())
      return false;
  }

  // else, acquire exclusive access to lock and remove it from the map without
  // destroying it (move it here)
  std::unique_lock wLock{m_mapMtx};
  auto it = m_map.find(sem);
  if (it == m_map.end())
    return false;
  SemaphoreContent semContent = std::move(it->second);
  m_map.erase(it);
  wLock.unlock();

  // collect until empty
  while (!semContent.allEmpty()) {
    uint64_t value = 0;
    AVK_VK_RST(dev.api()->vkGetSemaphoreCounterValue(dev.device(), sem, &value));
    semContent.collect(dev, value);
  }

  return true;
}

void VulkanDiscardPoolImpl::collect(VulkanDevice& dev) {
  std::shared_lock rLock{m_mapMtx};
  for (auto& [sem, content] : m_map) {
    uint64_t value = 0;
    AVK_VK_RST(dev.api()->vkGetSemaphoreCounterValue(dev.device(), sem, &value));
    content.collect(dev, value);
  }
}

void VulkanDiscardPoolImpl::collectSemaphore(VulkanDevice& dev, VkSemaphore sem) {
  std::shared_lock rLock{m_mapMtx};
  if (auto it = m_map.find(sem); it != m_map.end()) {
    uint64_t value = 0;
    AVK_VK_RST(dev.api()->vkGetSemaphoreCounterValue(dev.device(), sem, &value));
    it->second.collect(dev, value);
  }
}

bool VulkanDiscardPoolImpl::discardBuffer(VkSemaphore sem, uint64_t readyValue, VkBuffer buffer, VmaAllocation alloc) {
  std::shared_lock rLock{m_mapMtx};
  auto it = m_map.find(sem);
  if (it == m_map.end()) return false;

  return it->second.discardBuffer(buffer, alloc, readyValue);
}

bool VulkanDiscardPoolImpl::discardImage(VkSemaphore sem, uint64_t readyValue, VkImage image, VmaAllocation alloc) {
  std::shared_lock rLock{m_mapMtx};
  auto it = m_map.find(sem);
  if (it == m_map.end()) return false;

  return it->second.discardImage(image, alloc, readyValue);
}

// ------------------------------------------------------------------------------
// VulkanDiscardPool
// ------------------------------------------------------------------------------

VulkanDiscardPool::VulkanDiscardPool(VulkanDevice* dev) : m_dev(dev), m_impl(std::make_unique<VulkanDiscardPoolImpl>()) {
  assert(m_dev && m_impl);
}

VulkanDiscardPool::~VulkanDiscardPool() noexcept {
  assert(m_dev);
  m_impl->cleanup(*m_dev);
}

bool VulkanDiscardPool::registerTimelineSemaphore(VkSemaphore sem, uint32_t minResCap, uint32_t compactionThreshold, uint32_t maxResCap) {
  return m_impl->registerTimelineSemaphore(sem, minResCap, compactionThreshold, maxResCap);
}

bool VulkanDiscardPool::unregisterTimelineSemaphore(VkSemaphore sem) {
  assert(m_dev);
  return m_impl->unregisterTimelineSemaphore(*m_dev, sem);
}

void VulkanDiscardPool::collect() {
  assert(m_dev);
  m_impl->collect(*m_dev);
}

void VulkanDiscardPool::collectSemaphore(VkSemaphore sem) {
  assert(m_dev && sem);
  m_impl->collectSemaphore(*m_dev, sem);
}

bool VulkanDiscardPool::discardBuffer(VkSemaphore sem, uint64_t readyValue, VkBuffer buffer, VmaAllocation alloc) {
  return m_impl->discardBuffer(sem, readyValue, buffer, alloc);
}

bool VulkanDiscardPool::discardImage(VkSemaphore sem, uint64_t readyValue, VkImage image, VmaAllocation alloc) {
  return m_impl->discardImage(sem, readyValue, image, alloc);
}

}

