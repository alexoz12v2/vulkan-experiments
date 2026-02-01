#pragma once

// ------------------------------------------------------------------------
// Logging Related Macros (rely on iostream)
// ------------------------------------------------------------------------
#include <iostream>

#define LOG_RED "\033[31m"
#define LOG_YLW "\033[73m"
#define LOG_RST "\033[0m"
#define LOG_APP "[VulkanApp] "
#define LOG_ERR std::cerr << \
  LOG_RED LOG_APP << '[' << __FILE__ << ':' << __LINE__ << "] "
#define LOG_LOG std::cerr << \
  LOG_RST LOG_APP << '[' << __FILE__ << ':' << __LINE__ << "] "
#define AVK_VK_RST(expr) \
  do { \
    VkResult var = (expr); \
    if (var < 0) { \
      printf("[VulkanApp] [%s:%d] fatal vulkan error %d\n", __FILE__, __LINE__, var); \
      exit(1); \
    } \
  } while (0)

// ------------------------------------------------------------------------
// Vulkan Related Macros
// ------------------------------------------------------------------------

// https://docs.vulkan.org/spec/latest/appendices/boilerplate.html#boilerplate-wsi-header
#ifdef __APPLE__

#  define VK_USE_PLATFORM_METAL_EXT

#elif defined(_WIN32)

#  include <Windows.h>
#  define VK_USE_PLATFORM_WIN32_KHR

#elif defined(__linux__)

#  include <wayland-client.h>
#  define VK_USE_PLATFORM_WAYLAND_KHR

#else
#  error "unsupported"
#endif

// needed for portability subset extension name macro
#define VK_ENABLE_BETA_EXTENSIONS
#define VOLK_NO_DEVICE_PROTOTYPES

// VMA Configuration with volk
// https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/quick_start.html#quick_start_project_setup
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0

// VMA Memory Leak report
#define VMA_LEAK_LOG_FORMAT(format, ...) do { \
        printf((format), __VA_ARGS__); \
        printf("\n"); \
    } while(false)

// ------------------------------------------------------------------------
// Turn off code left for reference
// ------------------------------------------------------------------------
#if 0
// copied from spirv/unified1/spirv.h. somehow doesn't work including it
const char* SpvExecutionModelToString(SpvExecutionModel value) {
  switch (value) {
    case SpvExecutionModelVertex: return "Vertex";
    case SpvExecutionModelTessellationControl: return "TessellationControl";
    case SpvExecutionModelTessellationEvaluation: return "TessellationEvaluation";
    case SpvExecutionModelGeometry: return "Geometry";
    case SpvExecutionModelFragment: return "Fragment";
    case SpvExecutionModelGLCompute: return "GLCompute";
    case SpvExecutionModelKernel: return "Kernel";
    case SpvExecutionModelTaskNV: return "TaskNV";
    case SpvExecutionModelMeshNV: return "MeshNV";
    case SpvExecutionModelRayGenerationKHR: return "RayGenerationKHR";
    case SpvExecutionModelIntersectionKHR: return "IntersectionKHR";
    case SpvExecutionModelAnyHitKHR: return "AnyHitKHR";
    case SpvExecutionModelClosestHitKHR: return "ClosestHitKHR";
    case SpvExecutionModelMissKHR: return "MissKHR";
    case SpvExecutionModelCallableKHR: return "CallableKHR";
    case SpvExecutionModelTaskEXT: return "TaskEXT";
    case SpvExecutionModelMeshEXT: return "MeshEXT";
    default: return "Unknown";
  }
}

void reflectShader(std::filesystem::path const& path) {
  // outlive the reflect module I think? 
  // - (default) -> copied with calloc
  // - if flag SPV_REFLECT_MODULE_NO_COPY, then this must survive.
  std::vector<uint32_t> spvBin = readSpirv(path);
  if (spvBin.empty()) { 
    std::cerr << "could not read SPIR-V File at " << path.string() << std::endl;
    return;
  }

  SpvReflectShaderModule spvShaderModule{};
  SpvReflectResult res = spvReflectCreateShaderModule(spvBin.size() * sizeof(uint32_t), spvBin.data(), &spvShaderModule);
  if (res != SPV_REFLECT_RESULT_SUCCESS) {
    std::cerr << "spvReflectCreateShaderModule" << std::endl;
    return;
  }
  // clang-format off
  struct ModuleJanitor {
   public:
    ModuleJanitor(SpvReflectShaderModule& spvModule) : m_spvModule(spvModule) {}
    ~ModuleJanitor() noexcept { spvReflectDestroyShaderModule(&m_spvModule); }
   private:
    SpvReflectShaderModule& m_spvModule;
  } j(spvShaderModule);
  // clang-format on

  uint32_t descriptorSetCount = 0;
  res = spvReflectEnumerateDescriptorSets(&spvShaderModule, &descriptorSetCount, nullptr);
  if (res != SPV_REFLECT_RESULT_SUCCESS) {
    std::cerr << "spvReflectEnumerateDescriptorSets(null)" << std::endl;
    return;
  }
  std::vector<SpvReflectDescriptorSet*> sets(descriptorSetCount, nullptr);
  if (descriptorSetCount) {
    res = spvReflectEnumerateDescriptorSets(&spvShaderModule, &descriptorSetCount, sets.data());
    if (res != SPV_REFLECT_RESULT_SUCCESS) {
      std::cerr << "spvReflectEnumerateDescriptorSets(sets)" << std::endl;
      return;
    }
  }

  std::cout << "\nShader Module " << path.string() << '\n'
            << "------------------------------------------";
  if (spvShaderModule.entry_point_name) {
    std::cout << "\n'main' Entry Point: name: '" 
              << spvShaderModule.entry_point_name 
              << "' id: " << spvShaderModule.entry_point_id 
              << " with execution model: " 
              << SpvExecutionModelToString(spvShaderModule.spirv_execution_model);
  } else {
    std::cout << "\n'main' Entry Point: NULL, wait what?"; 
  }
  std::cout << "\nEntry Point Count:             " << spvShaderModule.entry_point_count
            << "\nCapability Count:              " << spvShaderModule.capability_count
            << "\nDescriptor Binding Count:      " << spvShaderModule.descriptor_binding_count
            << "\nDescriptor Set Count:          " << spvShaderModule.descriptor_set_count
            << "\nInput Variables Count:         " << spvShaderModule.input_variable_count
            << "\nOutput Variables Count:        " << spvShaderModule.output_variable_count
            << "\nInterface Variables Count:     " << spvShaderModule.interface_variable_count
            << "\nPush Constant Block Count:     " << spvShaderModule.push_constant_block_count
            << "\nSpecialization Constant Count: " << spvShaderModule.spec_constant_count
            << '\n';

  for (size_t i = 0; i < sets.size(); i++) {
    SpvReflectDescriptorSet const& reflSet = *sets[i];
    std::cout << "Descriptor Set " << i << ": {\n  binding_count: "
              << reflSet.binding_count << "\n  bindings: ["
              << (reflSet.binding_count == 0 ? "  ]\n" : "\n");
    for (size_t iBinding = 0; iBinding < reflSet.binding_count; ++iBinding) {
      SpvReflectDescriptorBinding const& binding = *reflSet.bindings[iBinding];
      std::cout << "   [" << iBinding << "] {\n    binding: " << binding.binding
                << "\n    descriptorType: " << static_cast<VkDescriptorType>(binding.descriptor_type)
                << "\n    descriptorCount: 1 ";
      for (uint32_t iDim = 0; iDim < binding.array.dims_count; ++iDim) {
        std::cout << "x " << binding.array.dims[iDim];
      }
      std::cout << "\n    stageFlags: 0x" << std::hex 
                << static_cast<VkShaderStageFlagBits>(spvShaderModule.shader_stage) 
                << std::dec << "\n    }\n";
    }
    std::cout << (reflSet.binding_count == 0 ? "" : "  ]\n");
  }
  std::cout << std::flush;
}
#endif

