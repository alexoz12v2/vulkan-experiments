#pragma once

#include "avkex-macros.h"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <thread>
#include <vector>

namespace avkex {

// ------------------------------------------------------------------------------
// RingBuffer
// ------------------------------------------------------------------------------

// latency-sensitive   -> threshold from 256 to 1024
// throughput-oriented -> threshold from 8k-64k 
// (depends on sizeof(T) and cost of move)
template <typename T>
class RingBuffer {
 public:
  explicit RingBuffer(size_t initialCap = 0, size_t bigThreshold = 1024)
   : m_bigThreshold(bigThreshold) { m_buffer.reserve(initialCap); }

  size_t size() const noexcept { return m_size; }
  bool empty() const noexcept { return m_size == 0; }

  T& operator[](size_t i) { assert(i < m_size); return m_buffer[m_start + i]; }
  T const& operator[](size_t i) const { assert(i < m_size); return m_buffer[m_start + i]; }

  T& front() { assert(m_size > 0); return m_buffer[m_start]; }
  T& back() { assert(m_size > 0); return m_buffer[m_start + m_size - 1]; }

  void push_back(T const& value) {
    maybeCompact();
    m_buffer.push_back(value);
    ++m_size;
  }
  void push_back(T&& value) {
    maybeCompact();
    m_buffer.push_back(std::move(value));
    ++m_size;
  }
  void pop_front() {
    assert(m_size > 0);
    ++m_start;
    --m_size;
  }
  void clear() {
    m_buffer.clear();
    m_start = 0;
    m_size = 0;
  }

 private:
  void maybeCompact() {
    // trigger compaction when dead space is large
    if (m_start < m_bigThreshold)
      return;
    std::move(m_buffer.begin() + m_start, m_buffer.begin() + m_start + m_size, m_buffer.begin());
    m_buffer.resize(m_size);
    m_start = 0;
  }

  std::vector<T> m_buffer;
  size_t m_start = 0;
  size_t m_size = 0;
  size_t m_bigThreshold;
};

// ------------------------------------------------------------------------------
// AtomicVector
// ------------------------------------------------------------------------------

template <typename T>
struct AtomicVector {
 private:
  static constexpr uint32_t ATOMIC_FREE = 0;
  static constexpr uint32_t ATOMIC_READING = 2;
  static constexpr uint32_t ATOMIC_WRITING = 1;

 public:
  explicit AtomicVector(uint32_t cap) { m_vec.reserve(cap); }

  // Unchecked move semantics: The application relies on assertions to be correct
  AtomicVector(AtomicVector&& that) noexcept 
   : m_vec(std::move(that.m_vec)),
     m_state(that.m_state.load(std::memory_order_relaxed)),
     m_refCount(that.m_refCount.load(std::memory_order_relaxed)) {
#ifdef AVK_DEBUG
    uint32_t const state = that.m_state.load(std::memory_order_relaxed);
    assert(state == ATOMIC_FREE || state == ATOMIC_WRITING);
    assert(that.m_refCount.load(std::memory_order_relaxed) == 0);
#endif

    that.m_state.store(ATOMIC_FREE, std::memory_order_relaxed);
    that.m_refCount.store(0, std::memory_order_relaxed);
  }

  AtomicVector& operator=(AtomicVector&& that) noexcept {
    if (this != &that) {
      // this and that should be free
#ifdef AVK_DEBUG
      uint32_t const thisState = m_state.load(std::memory_order_relaxed);
      uint32_t const thatState = that.m_state.load(std::memory_order_relaxed);
      assert(thisState == ATOMIC_FREE || thisState == ATOMIC_WRITING);
      assert(thatState == ATOMIC_FREE || thatState == ATOMIC_WRITING);
      assert(m_refCount.load(std::memory_order_relaxed) == 0);
      assert(that.m_refCount.load(std::memory_order_relaxed) == 0);
#endif
      m_vec = std::move(that.m_vec);
      m_state.store(that.m_state.load(std::memory_order_relaxed), std::memory_order_relaxed);
      m_refCount.store(that.m_refCount.load(std::memory_order_relaxed), std::memory_order_relaxed);

      that.m_state.store(ATOMIC_FREE, std::memory_order_relaxed);
      that.m_refCount.store(0, std::memory_order_relaxed);
    }
    return *this;
  }

  template <typename F> // TODO enable_if callable
  void readDo(F&& func) {
    std::vector<T> const& vec = acquireRead();
    func(vec);
    releaseRead(); // assumes func doesn't throw
  }

  template <typename F> // TODO enable_if callable
  void writeDo(F&& func) {
    std::vector<T> & vec = acquireWrite();
    func(vec);
    releaseWrite(); // assumes func doesn't throw
  }

  std::vector<T> const& acquireRead() {
    while (true) {
      uint32_t state = m_state.load(std::memory_order_relaxed);
      if (state != ATOMIC_FREE && state != ATOMIC_READING) {
        std::this_thread::yield();
        continue;
      }
        
      if (!m_state.compare_exchange_weak(state, ATOMIC_READING, std::memory_order_relaxed, std::memory_order_relaxed)) {
        std::this_thread::yield();
        continue;
      }
      
      m_refCount.fetch_add(1, std::memory_order_acquire); // memory barrier here
      return m_vec;
    }
    // [[unreachable]]; TODO Actual compiler dependent attribute
    return m_vec;
  }

  std::vector<T>& acquireWrite() {
    while (true) {
      uint32_t state = m_state.load(std::memory_order_relaxed);
      if (state != ATOMIC_FREE) {
        std::this_thread::yield();
        continue;
      }

      if (!m_state.compare_exchange_weak(state, ATOMIC_WRITING, std::memory_order_relaxed, std::memory_order_relaxed)) {
        std::this_thread::yield();
        continue;
      }
 
      m_state.load(std::memory_order_acquire); // memory barrier here
      return m_vec;
    }
    // [[unreachable]]; TODO Actual compiler dependent attribute
    return m_vec;
  }

  void releaseWrite() {
#ifdef AVK_DEBUG
    assert(m_state.load(std::memory_order_relaxed) == ATOMIC_WRITING);
#endif
    m_state.store(ATOMIC_FREE, std::memory_order_release); // memory barrier here
  }

  void releaseRead() {
#ifdef AVK_DEBUG
    assert(m_state.load(std::memory_order_relaxed) == ATOMIC_READING);
    assert(m_refCount.load(std::memory_order_relaxed) > 0);
#endif
    uint32_t const prevCount = m_refCount.fetch_sub(1, std::memory_order_relaxed);
    if (prevCount == 1) {
      m_state.store(ATOMIC_FREE, std::memory_order_release); // memory barrier here
    } else {
      m_state.store(ATOMIC_READING, std::memory_order_release); // memory barrier here
    }
  }

 private:
  std::vector<T> m_vec;
  std::atomic<uint32_t> m_state = 0;
  std::atomic<uint32_t> m_refCount = 0;
  static_assert(std::atomic<uint32_t>::is_always_lock_free);
};

// ------------------------------------------------------------------------------
// std::vector Extensions
// ------------------------------------------------------------------------------

template <typename T>
inline bool vectorPushWithGrowthLimit(std::vector<T>& vec, size_t cap, T const& val) {
  if (size_t oldCap = vec.capacity(); oldCap == vec.size()) {
    size_t const newCap = 2 * oldCap;
    if (newCap > cap)
      return false;
    vec.reserve(newCap);
  }
  vec.push_back(val);
  return true;
}

template <typename T>
inline bool vectorPushWithGrowthLimit(std::vector<T>& vec, size_t cap, T&& val) {
  if (size_t oldCap = vec.capacity(); oldCap == vec.size()) {
    size_t const newCap = 2 * oldCap;
    if (newCap > cap)
      return false;
    vec.reserve(newCap);
  }
  vec.push_back(std::move(val));
  return true;
}

template <typename T, typename... Args>
inline bool vectorEmplaceWithGrowthLimit(std::vector<T>& vec, size_t cap, Args&&... args) {
  if (size_t oldCap = vec.capacity(); oldCap == vec.size()) {
    size_t const newCap = 2 * oldCap;
    if (newCap > cap)
      return false;
    vec.reserve(newCap);
  }
  vec.emplace_back(std::forward<Args>(args)...);
  return true;
}

}

