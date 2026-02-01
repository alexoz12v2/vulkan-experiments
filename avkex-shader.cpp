#include "avkex.h"

#include <atomic>
#include <iostream>
#include <unordered_map>
#include <thread>
#include <shared_mutex>
#include <string>

// Note: Shader modules are not the only way to provide SPIR-V Code to
// the Vulkan Runtime
// https://docs.vulkan.org/guide/latest/ways_to_provide_spirv.html

using namespace avkex;

namespace avkex {

class ShaderData {
 public:
  ShaderData(ShaderData const&) = delete;
  ShaderData(ShaderData &&) noexcept = default;
  ShaderData& operator=(ShaderData const&) = delete;
  ShaderData& operator=(ShaderData &&) noexcept = default;
#ifdef AVK_DEBUG
  ~ShaderData() noexcept { assert(m_shaderModule == VK_NULL_HANDLE && "cleanup not called"); }
#endif

  ShaderData(VulkanDevice& dev, uint32_t const* pCode, uint32_t wordCount) : m_pCode(pCode, pCode + wordCount) {
    size_t const codeSize = static_cast<size_t>(wordCount) * sizeof(uint32_t);
    // 1. reflect data
    SpvReflectResult const res = spvReflectCreateShaderModule2(
      SPV_REFLECT_MODULE_FLAG_NO_COPY, codeSize, m_pCode.data(), &m_spvShaderModule);
    if (res != SPV_REFLECT_RESULT_SUCCESS) {
      m_spvShaderModule.entry_point_name = nullptr;
      LOG_ERR << "Error in spvReflectCreateShaderModule" LOG_RST << std::endl;
    }

    // 2. create shader module (TODO non crashing failure)
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = codeSize;
    createInfo.pCode = pCode;
    AVK_VK_RST(dev.api()->vkCreateShaderModule(dev.device(), &createInfo, nullptr, &m_shaderModule));
  }

  void cleanup(VulkanDevice& dev) noexcept {
    // 1. destroy shader module
    dev.api()->vkDestroyShaderModule(dev.device(), m_shaderModule, nullptr);
    m_shaderModule = VK_NULL_HANDLE;
    // 2. destory reflection data
    spvReflectDestroyShaderModule(&m_spvShaderModule);
  }

  VkShaderModule shaderModule() const { return m_shaderModule; }
  // be ware of how you use a getter to a reference
  SpvReflectShaderModule const& spvShaderModule() const { return m_spvShaderModule; }

 private:
  std::vector<uint32_t> m_pCode;
  SpvReflectShaderModule m_spvShaderModule = {};
  VkShaderModule m_shaderModule = VK_NULL_HANDLE;
};

// ------------------------------------------------------------------------------
// VulkanShaderRegistryImpl
// ------------------------------------------------------------------------------
class VulkanShaderRegistryImpl {
 public:
  VulkanShaderRegistryImpl(size_t minCap, size_t maxCap) : m_maxCap(maxCap) {
    m_map.reserve(minCap);
  }

  bool registerShader(VulkanDevice& dev, std::string_view name, uint32_t const* pCode, uint32_t wordCount);
  bool unregisterShader(VulkanDevice& dev, std::string_view name);
  bool withShader(std::string_view name, std::function<void(VkShaderModule, SpvReflectShaderModule const&)> const& func) const;
  void cleanup(VulkanDevice& dev) noexcept;

 private:
  size_t m_maxCap;
  mutable std::shared_mutex m_mapMtx;
  // note: you can avoid constructing a std string and hash the string view
  std::unordered_map<std::string, ShaderData> m_map;
};

bool VulkanShaderRegistryImpl::registerShader(VulkanDevice& dev, std::string_view name, uint32_t const* pCode, uint32_t wordCount) {
  std::lock_guard wLock{m_mapMtx};
  // 1. already exists
  if (m_map.find(std::string(name)) != m_map.end()) {
    return false;
  }
  // 2. capacity reached
  if (m_map.size() >= m_maxCap) {
    return false;
  }
  // 3. try insertion (someone else might have acquired lock)
  auto [it, wasInserted] = m_map.try_emplace(std::string(name), dev, pCode, wordCount);
  return wasInserted;
}

bool VulkanShaderRegistryImpl::unregisterShader(VulkanDevice& dev, std::string_view name) {
  std::lock_guard wLock{m_mapMtx};
  auto it = m_map.find(std::string(name)); 
  if (it == m_map.end()) {
    return false;
  }
  it->second.cleanup(dev);
  m_map.erase(it);
  return true;
}

bool VulkanShaderRegistryImpl::withShader(std::string_view name, std::function<void(VkShaderModule, SpvReflectShaderModule const&)> const& func) const {
  std::shared_lock rLock{m_mapMtx};
  if (auto it = m_map.find(std::string(name)); it != m_map.end()) {
    func(it->second.shaderModule(), it->second.spvShaderModule());
    return true;
  }
  return false;
}

void VulkanShaderRegistryImpl::cleanup(VulkanDevice& dev) noexcept {
  std::lock_guard wLock{m_mapMtx};
  while (!m_map.empty()) {
    auto it = m_map.begin();
    it->second.cleanup(dev);
    m_map.erase(it);
  }
}

// ------------------------------------------------------------------------------
// VulkanShaderRegistry
// ------------------------------------------------------------------------------

VulkanShaderRegistry::VulkanShaderRegistry(VulkanDevice* dev, size_t minCap, size_t maxCap)
 : m_impl(std::make_unique<VulkanShaderRegistryImpl>(minCap, maxCap)) {
  dev->acquire();
  m_dev = dev;
}

VulkanShaderRegistry::~VulkanShaderRegistry() noexcept {
  m_impl->cleanup(*m_dev);
  m_impl.release();
  m_dev->release();
  m_dev = nullptr;
}

bool VulkanShaderRegistry::registerShader(std::string_view name, uint32_t const* pCode, uint32_t wordCount) {
  return m_impl->registerShader(*m_dev, name, pCode, wordCount);
}

bool VulkanShaderRegistry::unregisterShader(std::string_view name) {
  return m_impl->unregisterShader(*m_dev, name);
}

bool VulkanShaderRegistry::withShader(std::string_view shader, std::function<void(VkShaderModule, SpvReflectShaderModule const&)> const& func) const {
  return m_impl->withShader(shader, func);
}

}
