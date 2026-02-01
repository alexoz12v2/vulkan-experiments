#include <algorithm>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <fstream>
#include <vector>

#include "avkex.h"
#include "avkex-os.h"

// TODO Move
namespace {

std::vector<uint32_t> readSpirv(std::filesystem::path const& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) return {};
  std::streamsize const size = file.tellg();
  assert((size & sizeof(uint32_t) - 1) == 0); // SPIR-V is 4-byte aligned
  file.seekg(0, std::ios::beg);

  // TODO: is this translated into assembly as the left shift by the popcount?
  std::vector<uint32_t> words(size / sizeof(uint32_t));
  file.read(reinterpret_cast<char*>(words.data()), size);
  assert(words.size() >= 5); // SPIR-V header is 5 words
  return words;
}

// TODO abstract descriptor set management
// this automates creation for exactly a layout on a shader.
void fillDescriptorSets(avkex::VulkanDevice& dev, VkDescriptorPool descriptorPool, std::vector<VkDescriptorSetLayout> const& setLayouts, std::vector<VkDescriptorSet>* outSets) {
  assert(outSets && setLayouts.size() > 0);
  outSets->clear();
  outSets->resize(setLayouts.size());
  VkDescriptorSetAllocateInfo allocateInfo{};
  allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocateInfo.descriptorPool = descriptorPool;
  allocateInfo.descriptorSetCount = static_cast<uint32_t>(setLayouts.size());
  allocateInfo.pSetLayouts = setLayouts.data();
  AVK_VK_RST(dev.api()->vkAllocateDescriptorSets(dev.device(), &allocateInfo, outSets->data()));
}

// Memory stuff ----------
// TODO: for now, assumes that VK_EXT_memory_budget is active

bool isVramTight(avkex::VulkanDevice& dev) {
#if 0
  VkPhysicalDeviceMemoryProperties2 memoryProperties{};
  VkPhysicalDeviceMemoryBudgetPropertiesEXT budgetProperties{};
  memoryProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
  budgetProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
  memoryProperties.pNext = &budgetProperties;
  // TODO: Apparently volk doens't have this, use vkGetDeviceProcAddr
  assert(dev.api()->vkGetPhysicalDeviceMemoryProperties2KHR);
  dev.api()->vkGetPhysicalDeviceMemoryProperties2KHR(dev.device(), &memoryProperties);

  std::vector<VmaBudget> memBudget(memoryProperties.memoryProperties.memoryHeapCount);
  vmaGetHeapBudgets(dev.allocator(), memBudget.data());
  // for now, we'll consider only the budget of the first device local memory
  uint32_t firstDeviceLocalHeap = -1U;
  for (uint32_t heap = 0; heap < memoryProperties.memoryProperties.memoryHeapCount; ++heap) {
    VkMemoryHeap const& memHeap = memoryProperties.memoryProperties.memoryHeaps[heap];
    // TODO: for now, we won't check whether this is reBAR or not (discrete gpu)
    // would require finding all memory types belonging to heap
    if (memHeap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
      firstDeviceLocalHeap = heap;
      break;
    }
  }
  assert(firstDeviceLocalHeap >= 0);
  VkDeviceSize const budget = memBudget[firstDeviceLocalHeap].budget;
  VkDeviceSize const usage = memBudget[firstDeviceLocalHeap].usage;
  if (budget > 0) {
    float const percentage = static_cast<float>(usage) / static_cast<float>(budget);
    static float constexpr PERCENTAGE_THRESHOLD = 0.85f;
    if (percentage > PERCENTAGE_THRESHOLD) {
      LOG_LOG << "Warning: GPU VRAM > 85% full. Forcing fallback paths." << std::endl;
      return true;
    }
  }
#endif
  return false;
}

// TODO thread local class with discard and recycle mechanism
VkDescriptorPool basicDescriptorPool(avkex::VulkanDevice& dev) {
  static uint32_t constexpr MAX_SETS = 64;
  static uint32_t constexpr DESCRIPTOR_TYPE_COUNT = 2;
  static uint32_t constexpr UNIFORM_BUFFER_COUNT = 16;
  static uint32_t constexpr STORAGE_BUFFER_COUNT = 16;
  // TODO: Inline uniform block/mutable descriptor
  VkDescriptorPool descriptorPool = VK_NULL_HANDLE;

  VkDescriptorPoolSize poolSizes[DESCRIPTOR_TYPE_COUNT]{};
  poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  poolSizes[0].descriptorCount = UNIFORM_BUFFER_COUNT;
  poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  poolSizes[1].descriptorCount = STORAGE_BUFFER_COUNT;

  VkDescriptorPoolCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  createInfo.maxSets = MAX_SETS;
  createInfo.poolSizeCount = DESCRIPTOR_TYPE_COUNT;
  createInfo.pPoolSizes = poolSizes;

  // should handle explicitly out of memory errors?
  AVK_VK_RST(dev.api()->vkCreateDescriptorPool(dev.device(), &createInfo, nullptr, &descriptorPool));
  return descriptorPool;
}

void doSaxpy(avkex::VulkanDevice& dev, VkPipelineLayout pipelineLayout, VkPipeline computePipeline, avkex::VulkanCommandBufferManager& commandBufferManager, std::vector<VkDescriptorSet> const& descriptorSets) {
  static size_t constexpr ELEMENT_COUNT = 1024;
  std::vector<float> h_a;
  std::vector<float> h_b;
  float const scalar = 2.f;
  std::vector<float> h_c;
  h_a.reserve(ELEMENT_COUNT);
  h_b.reserve(ELEMENT_COUNT);
  for (size_t i = 0; i < h_a.capacity(); ++i) {
    h_a.push_back(i + 1);
    h_b.push_back(ELEMENT_COUNT - (i + 1));
  }

  // buffer handles
  VkBuffer d_buffer = VK_NULL_HANDLE; // a | b | scalar
  VmaAllocation alloc = VK_NULL_HANDLE;
  VkBuffer d_staging = VK_NULL_HANDLE;
  VmaAllocation stagingAlloc = VK_NULL_HANDLE;

  // event to signal kernel completion
  // Note: On apple requires VkPhysicalDevicePortabilitySubsetFeaturesKHR::events
  // just to try it out, it should be used among different command buffers in same queue
  VkEvent evKernelDone = VK_NULL_HANDLE;
  {
    VkEventCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_EVENT_CREATE_INFO;
    // TODO: is it needed to export it onto metal?
    // if VK_KHR_synchronization2(prom. 1.3) -> VK_EVENT_CREATE_DEVICE_ONLY_BIT
    AVK_VK_RST(dev.api()->vkCreateEvent(dev.device(), &createInfo, nullptr, &evKernelDone));
  }

  // Variables used as backing for allocation info
  VmaAllocationInfo allocInfo{};
  VkMemoryPropertyFlags memPropertyFlags;
  VmaAllocationInfo stagingAllocInfo{};

  size_t const inputBytes = (2 * ELEMENT_COUNT + 1) * sizeof(float);

  VkCommandBuffer commandBuffer = commandBufferManager.getThreadLocalComputeCommandBufferForTimeline(0);
  vmaSetCurrentFrameIndex(dev.allocator(), 0);

  // 1. allocate buffers  
  //  - Input: 1 Staging (if necessary) + 2 GPU-Only
  bool const forceStaging = isVramTight(dev); // TODO better

  // - ask for "Host Sequential Write" (CPU can map it)
  // - if reBAR(Discrete)/Unified memory(integrated/SoC) available, then both device local and host visible. Otherwise, allocate staging
  VmaAllocationCreateInfo allocCreateInfo{};
  allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
  allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
    VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT |
    VMA_ALLOCATION_CREATE_MAPPED_BIT;

  VkBufferCreateInfo bufferCreateInfo{};
  bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferCreateInfo.size = inputBytes;
  bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
    VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

  VkResult vkResult = vmaCreateBuffer(dev.allocator(), &bufferCreateInfo, &allocCreateInfo, &d_buffer, &alloc, &allocInfo);
  AVK_VK_RST(vkResult);
  vmaGetAllocationMemoryProperties(dev.allocator(), alloc, &memPropertyFlags);

  // 2. start command buffer
  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  // should explicitly check for out of memory?
  AVK_VK_RST(dev.api()->vkBeginCommandBuffer(commandBuffer, &beginInfo));

  // 2.5 update descriptor sets (buffer ↔ descriptor)
  static uint32_t constexpr DESCRIPTOR_WRITE_COUNT = 3; // TODO might be dyn
  VkWriteDescriptorSet descriptorWrites[DESCRIPTOR_WRITE_COUNT]{};
  VkDescriptorBufferInfo bufferInfos[DESCRIPTOR_WRITE_COUNT]{};
  assert(descriptorSets.size() == 1);
  for (uint32_t i = 0; i < DESCRIPTOR_WRITE_COUNT; ++i) {
    descriptorWrites[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[i].dstSet = descriptorSets[0];
    descriptorWrites[i].descriptorCount = 1;
    descriptorWrites[i].dstBinding = i;
    descriptorWrites[i].pBufferInfo = &bufferInfos[i];
  }

  descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; // data_in
  bufferInfos[0].buffer = d_buffer;
  bufferInfos[0].offset = static_cast<VkDeviceSize>(0);
  bufferInfos[0].range = static_cast<VkDeviceSize>(ELEMENT_COUNT * sizeof(float));

  descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; // data_out
  bufferInfos[1].buffer = d_buffer;
  bufferInfos[1].offset = static_cast<VkDeviceSize>(ELEMENT_COUNT * sizeof(float));
  bufferInfos[1].range = static_cast<VkDeviceSize>(ELEMENT_COUNT * sizeof(float));

  descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; // scalar_a 
  bufferInfos[2].buffer = d_buffer;
  bufferInfos[2].offset = static_cast<VkDeviceSize>(2 * ELEMENT_COUNT * sizeof(float));
  bufferInfos[2].range = static_cast<VkDeviceSize>(sizeof(float)); // equivalent to VK_WHOLE_RANGE in this case

  dev.api()->vkUpdateDescriptorSets(dev.device(), DESCRIPTOR_WRITE_COUNT, descriptorWrites, 0, nullptr);

  // 3. bind descriptor sets
  // VK_KHR_maintenance6?
  dev.api()->vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, static_cast<uint32_t>(descriptorSets.size()), descriptorSets.data(), 0, nullptr);

  // 4. transfer memory to device buffer
  //  - input transfer
  if (memPropertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
    // Input in Mappable memory: (reBAR or Unified)
    // transfer host -> device |barrier| -> CS
    // one shot map -> cpy -> unmap -> flush: vmaCopyMemoryToAllocation. For multiple ops, need explicit sequence
    void* mapped = nullptr;
    AVK_VK_RST(vmaMapMemory(dev.allocator(), alloc, &mapped));
    memcpy(mapped, h_a.data(), h_a.size() * sizeof(float));
    memcpy(reinterpret_cast<uint8_t*>(mapped) + h_a.size() * sizeof(float), h_b.data(), h_b.size() * sizeof(float));
    memcpy(reinterpret_cast<uint8_t*>(mapped) + 2 * h_a.size() * sizeof(float), &scalar, sizeof(float));
    vmaUnmapMemory(dev.allocator(), alloc);
    AVK_VK_RST(vmaFlushAllocation(dev.allocator(), alloc, 0, inputBytes));

    // barrier to ensure writing finished before pipeline execution
    VkBufferMemoryBarrier memoryBarrier{};
    memoryBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    memoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    memoryBarrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
    memoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    memoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    memoryBarrier.buffer = d_buffer;
    memoryBarrier.offset = 0;
    memoryBarrier.size = VK_WHOLE_SIZE; // inputBytes
    dev.api()->vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &memoryBarrier, 0, nullptr);
  } else {
    // Input to non mappable memory. Create a staging buffer on the fly (VMA makes it cheaper with its block suballocation system)
    // transfer host -> staging |barrier| staging -> device |barrier| -> CS
    VkBufferCreateInfo stagingBufCreateInfo{};
    stagingBufCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufCreateInfo.size = inputBytes;
    stagingBufCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo stagingAllocCreateInfo{};
    stagingAllocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAllocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
      VMA_ALLOCATION_CREATE_MAPPED_BIT;
    vkResult = vmaCreateBuffer(dev.allocator(), &stagingBufCreateInfo, &stagingAllocCreateInfo, &d_staging, &stagingAlloc, &stagingAllocInfo);
    AVK_VK_RST(vkResult);

    void* mapped = nullptr; // TODO deduplicate
    AVK_VK_RST(vmaMapMemory(dev.allocator(), stagingAlloc, &mapped));
    memcpy(mapped, h_a.data(), h_a.size() * sizeof(float));
    memcpy(reinterpret_cast<uint8_t*>(mapped) + h_a.size() * sizeof(float), h_b.data(), h_b.size() * sizeof(float));
    memcpy(reinterpret_cast<uint8_t*>(mapped) + h_a.size() * sizeof(float), &scalar, sizeof(float));
    vmaUnmapMemory(dev.allocator(), alloc);
    AVK_VK_RST(vmaFlushAllocation(dev.allocator(), stagingAlloc, 0, inputBytes));

    // barrier before transfer
    VkBufferMemoryBarrier stagingTransferMemoryBarrier{};
    stagingTransferMemoryBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    stagingTransferMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    stagingTransferMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    stagingTransferMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    stagingTransferMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    stagingTransferMemoryBarrier.buffer = d_staging;
    stagingTransferMemoryBarrier.offset = 0;
    stagingTransferMemoryBarrier.size = VK_WHOLE_SIZE; // inputBytes

    dev.api()->vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
      0, 0, nullptr, 1, &stagingTransferMemoryBarrier, 0, nullptr);

    // buffer copy operation with its barrier
    VkBufferCopy bufCopy{};
    bufCopy.srcOffset = 0;
    bufCopy.dstOffset = 0;
    bufCopy.size = inputBytes;

    dev.api()->vkCmdCopyBuffer(commandBuffer, d_staging, d_buffer, 1, &bufCopy);

    VkBufferMemoryBarrier copyCsMemoryBarrier{};
    copyCsMemoryBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    copyCsMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    copyCsMemoryBarrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
    copyCsMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    copyCsMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    copyCsMemoryBarrier.buffer = d_buffer;
    copyCsMemoryBarrier.offset = 0;
    copyCsMemoryBarrier.size = VK_WHOLE_SIZE; // inputBytes

    dev.api()->vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      0, 0, nullptr, 1, &copyCsMemoryBarrier, 0, nullptr);
  }

  // 4.1 bind and execute compute pipeline
  dev.api()->vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);

  // this is the equivalent of the CUDA Grid, ie gridDim
  // TODO: For now local group size (CUDA Block) is baked into the shader. expose it as a specialization constant (which can be baked into pipeline instead or dynamic state)
  uint32_t const groupCountX = ELEMENT_COUNT; 
  uint32_t const groupCountY = 1;
  uint32_t const groupCount = 1;
  dev.api()->vkCmdDispatch(commandBuffer, groupCountX, groupCountY, groupCount);

  // that's basically an execution barrier. I just wanted to try out events.
  VkBufferMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
  barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  barrier.dstAccessMask = !(memPropertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_HOST_READ_BIT;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.buffer = d_buffer;
  barrier.offset = ELEMENT_COUNT * sizeof(float);
  barrier.size = ELEMENT_COUNT * sizeof(float);
  dev.api()->vkCmdSetEvent(commandBuffer, evKernelDone, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
  dev.api()->vkCmdWaitEvents(commandBuffer, 1, &evKernelDone, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, !(memPropertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_HOST_BIT, 0, nullptr, 1, &barrier, 0, nullptr);

  // if using staging, then transfer to it such that we can use it later
  if (!(memPropertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
    VkBufferCopy bufCopy{};
    bufCopy.srcOffset = ELEMENT_COUNT * sizeof(float);
    bufCopy.dstOffset = ELEMENT_COUNT * sizeof(float);
    bufCopy.size = ELEMENT_COUNT * sizeof(float);
    dev.api()->vkCmdCopyBuffer(commandBuffer, d_buffer, d_staging, 1, &bufCopy);

    // reverse barrier from transfer to host stage
    VkBufferMemoryBarrier memBarrier{};
    memBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    memBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    memBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    memBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    memBarrier.buffer = d_staging;
    memBarrier.offset = ELEMENT_COUNT * sizeof(float);
    memBarrier.size = ELEMENT_COUNT * sizeof(float);
    dev.api()->vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
      0, 0, nullptr, 1, &memBarrier, 0, nullptr);
  }

  // 5. submit with fence
  AVK_VK_RST(dev.api()->vkEndCommandBuffer(commandBuffer));

  VkSemaphore const signalSemaphore = dev.computeTimelineSemaphore();
  uint64_t const signalSemaphoreValue = 1;

  VkTimelineSemaphoreSubmitInfo semaphoreSubmitInfo{};
  semaphoreSubmitInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
  semaphoreSubmitInfo.signalSemaphoreValueCount = 1;
  semaphoreSubmitInfo.pSignalSemaphoreValues = &signalSemaphoreValue;

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.pNext = &semaphoreSubmitInfo;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = &signalSemaphore;

  // should we use VK_KHR_synchronization2? Not all android support it
  // should we use a fence? We have the timeline semaphore, so not strictly necessary
  AVK_VK_RST(dev.api()->vkQueueSubmit(dev.computeQueue(), 1, &submitInfo, VK_NULL_HANDLE));

  // 6. wait on the fence and print result
  VkSemaphoreWaitInfo waitInfo{};
  waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
  waitInfo.semaphoreCount = 1;
  waitInfo.pSemaphores = &signalSemaphore;
  waitInfo.pValues = &signalSemaphoreValue;
  AVK_VK_RST(dev.api()->vkWaitSemaphoresKHR(dev.device(), &waitInfo, UINT64_MAX));

  // 7 write back to CPU side host buffer after a barrier
  h_c.clear();
  h_c.resize(ELEMENT_COUNT);
  void* mapped = nullptr;
  AVK_VK_RST(vmaMapMemory(dev.allocator(), (memPropertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) ? alloc : stagingAlloc, &mapped));
  memcpy(h_c.data(), reinterpret_cast<uint8_t*>(mapped) + ELEMENT_COUNT * sizeof(float), ELEMENT_COUNT * sizeof(float));
  vmaUnmapMemory(dev.allocator(), alloc);
  AVK_VK_RST(vmaFlushAllocation(dev.allocator(), alloc, ELEMENT_COUNT * sizeof(float), ELEMENT_COUNT * sizeof(float)));
  // no need for a barrier, as we've finished here

  // cleanup and print result
  vmaDestroyBuffer(dev.allocator(), d_buffer, alloc);
  if (stagingAlloc != VK_NULL_HANDLE) {
    assert(d_staging != VK_NULL_HANDLE);
    vmaDestroyBuffer(dev.allocator(), d_staging, stagingAlloc);
  }
  dev.api()->vkDestroyEvent(dev.device(), evKernelDone, nullptr);

  // finally print
  LOG_LOG << "saxpy kernel executed: result[0]: " << h_c[0] << std::endl;
}

}

int main() {
  namespace fs = std::filesystem;
  std::cout << "Hello World" << std::endl;
  fs::path exeDir = *avkex::os::getExecutableDirectory();
  std::cout << "Executable directory is '" << exeDir.string() << "'\n" << std::flush;

  avkex::VulkanApp app;
  std::vector<avkex::VulkanDeviceInfo> devs = app.getEligibleDevices();
  if (devs.empty()) {
    LOG_ERR << "No vulkan capable devices found. Crashing..." LOG_RST << std::endl;
    return 1;
  }
  LOG_LOG << "Found " << devs.size() << " Vulkan Capable GPUs. Choose first" << std::endl;
  { // ensure device dies before instance
    avkex::VulkanDevice device(app.instance(), devs[0]);
    { // ensure device users die before device
      avkex::VulkanCommandBufferManager commandBufferManager(&device);
      avkex::VulkanShaderRegistry shaderRegistry(&device);
      bool bRes = shaderRegistry.registerShader("saxpy", readSpirv(exeDir / "shaders" / "saxpy.first.spv"));
      assert(bRes);

      VkDescriptorPool descriptorPool = basicDescriptorPool(device);
      std::vector<VkDescriptorSet> descriptorSets;
      descriptorSets.reserve(64);

      std::vector<VkDescriptorSetLayout> computeShaderDescriptorSetLayouts;
      VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
      VkPipeline computePipeline = VK_NULL_HANDLE; 
      computeShaderDescriptorSetLayouts.reserve(64);
      shaderRegistry.withShader("saxpy", [&](VkShaderModule shaderModule, SpvReflectShaderModule const& spvShaderModule) {
        std::vector<avkex::VulkanDescriptorSetLayoutData> layoutData = avkex::reflectShaderDescriptors(spvShaderModule);
        computeShaderDescriptorSetLayouts.resize(layoutData.size());
        std::transform(layoutData.cbegin(), layoutData.cend(), computeShaderDescriptorSetLayouts.begin(), 
          [&device](avkex::VulkanDescriptorSetLayoutData const& x) -> VkDescriptorSetLayout {
            return avkex::createDescriptorSetLayout(device, x); 
        });

        // now pipeline layout and pipeline
        pipelineLayout = avkex::createPipelineLayout(
          device, static_cast<uint32_t>(computeShaderDescriptorSetLayouts.size()), computeShaderDescriptorSetLayouts.data());
        assert(pipelineLayout != VK_NULL_HANDLE);

        fillDescriptorSets(device, descriptorPool, computeShaderDescriptorSetLayouts, &descriptorSets);

        computePipeline = avkex::createComputePipeline(
          device, pipelineLayout, spvShaderModule, shaderModule);
        assert(computePipeline != VK_NULL_HANDLE);

        LOG_LOG << "Created Compute Pipeline🎉!" << std::endl;
      });

      // execution
      doSaxpy(device, pipelineLayout, computePipeline, commandBufferManager, descriptorSets);

      // cleanup (TODO Refactor into classes)
      device.api()->vkDestroyPipeline(device.device(), computePipeline, nullptr);
      device.api()->vkDestroyPipelineLayout(device.device(), pipelineLayout, nullptr);
      for (VkDescriptorSetLayout layout : computeShaderDescriptorSetLayouts) {
        device.api()->vkDestroyDescriptorSetLayout(device.device(), layout, nullptr);
      }
      // automatically frees descriptor sets
      device.api()->vkDestroyDescriptorPool(device.device(), descriptorPool, nullptr);
    }
  }
}

