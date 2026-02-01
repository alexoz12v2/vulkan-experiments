#include "avkex.h"

using namespace avkex;

namespace {

void fillShaderStage(VulkanDevice& dev, VkPipelineShaderStageCreateInfo& createInfo, SpvReflectShaderModule const& spvShaderModule, VkShaderModule shaderModule);

}

namespace avkex {

std::vector<VulkanDescriptorSetLayoutData> reflectShaderDescriptors(SpvReflectShaderModule const& spvShaderModule) {
  std::vector<VulkanDescriptorSetLayoutData> data;
  data.resize(spvShaderModule.descriptor_set_count);

  std::vector<SpvReflectDescriptorSet*> sets;
  sets.reserve(64);
  uint32_t setCount = 0;
  SpvReflectResult res = spvReflectEnumerateDescriptorSets(&spvShaderModule, &setCount, nullptr);
  assert(res == SPV_REFLECT_RESULT_SUCCESS && "spvReflectEnumerateDescriptorSets(null)");
  sets.clear();
  if (setCount > 0) {
    sets.resize(setCount);
    data.resize(setCount);
    res = spvReflectEnumerateDescriptorSets(&spvShaderModule, &setCount, sets.data());
    assert(res == SPV_REFLECT_RESULT_SUCCESS && "spvReflectEnumerateDescriptorSets(sets)");
  }

  uint32_t i = 0;
  for (SpvReflectDescriptorSet* pSpvSet : sets) {
    uint32_t const index = i++;
    data[index].setNumber = index;
    data[index].createInfo = {};
    data[index].createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    data[index].createInfo.bindingCount = pSpvSet->binding_count;
    data[index].bindings.resize(pSpvSet->binding_count);
    data[index].createInfo.pBindings = data[index].bindings.data();

    uint32_t j = 0;
    for (VkDescriptorSetLayoutBinding& binding : data[index].bindings) {
      uint32_t const iBinding = j++;
      SpvReflectDescriptorBinding const& spvBinding = *pSpvSet->bindings[iBinding];
      binding.binding = spvBinding.binding;
      binding.descriptorType = static_cast<VkDescriptorType>(spvBinding.descriptor_type);
      // TODO: for now assume we refer to the first entry point
      binding.stageFlags = static_cast<VkShaderStageFlagBits>(spvShaderModule.shader_stage);
      binding.descriptorCount = 1;
      for (uint32_t iDim = 0; iDim < spvBinding.array.dims_count; ++iDim) {
        binding.descriptorCount *= spvBinding.array.dims[iDim];
      }
    }
  }

  return data;
}

VkDescriptorSetLayout createDescriptorSetLayout(VulkanDevice& dev, VulkanDescriptorSetLayoutData const& setLayoutData) {
  // 1. Query support for desired layout on the device. If not supported, return VK_NULL_HANDLE
  VkDescriptorSetLayoutSupport support{};
  support.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_SUPPORT;
  // TODO maybe: if VK_EXT_descriptor_indexing is used (per descriptor flags) -> pNext needed
  dev.api()->vkGetDescriptorSetLayoutSupport(dev.device(), &setLayoutData.createInfo, &support);
  if (support.supported == VK_FALSE) return VK_NULL_HANDLE;

  // 2. Try to create Descriptor Set Layout
  VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
  AVK_VK_RST(dev.api()->vkCreateDescriptorSetLayout(dev.device(), &setLayoutData.createInfo, nullptr, &descriptorSetLayout));
  return descriptorSetLayout;
}

VkPipelineLayout createPipelineLayout(VulkanDevice& dev, uint32_t setLayoutCount, VkDescriptorSetLayout const* pSetLayouts, uint32_t pushConstantRangeCount, VkPushConstantRange const* pPushConstantRanges) {
  VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
  VkPipelineLayoutCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  createInfo.setLayoutCount = setLayoutCount;
  createInfo.pSetLayouts = pSetLayouts;
  createInfo.pushConstantRangeCount = pushConstantRangeCount;
  createInfo.pPushConstantRanges = pPushConstantRanges;

  AVK_VK_RST(dev.api()->vkCreatePipelineLayout(dev.device(), &createInfo, nullptr, &pipelineLayout));
  return pipelineLayout;
}

VkPipeline createComputePipeline(VulkanDevice& dev, VkPipelineLayout pipelineLayout, SpvReflectShaderModule const& spvShaderModule, VkShaderModule shaderModule) {
  VkPipeline pipeline = VK_NULL_HANDLE;

  VkComputePipelineCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  createInfo.layout = pipelineLayout;
  fillShaderStage(dev, createInfo.stage, spvShaderModule, shaderModule);

  // TODO pipeline caching (meaning you need to retain created VkShaderModule too)
  // TODO pipeline binary
  AVK_VK_RST(dev.api()->vkCreateComputePipelines(
    dev.device(), VK_NULL_HANDLE, 1, &createInfo, nullptr, &pipeline));
  return pipeline;
}

}

namespace {

void fillShaderStage(VulkanDevice& dev, VkPipelineShaderStageCreateInfo& createInfo, SpvReflectShaderModule const& spvShaderModule, VkShaderModule shaderModule) {
  createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  createInfo.stage = static_cast<VkShaderStageFlagBits>(spvShaderModule.shader_stage);

  // TODO: don't assume we want to take the main entrypoint
  createInfo.pName = spvShaderModule.entry_point_name;

  // TODO: specialization info handling
  assert(spvShaderModule.spec_constant_count == 0);

  createInfo.module = shaderModule;
}

}

