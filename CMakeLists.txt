cmake_minimum_required(VERSION 4.1)

# clone vcpkg and set VCPKG_ROOT
if (NOT VCPKG_COMMAND)
  include(FetchContent)
  FetchContent_Declare(vcpkg
    GIT_REPOSITORY https://github.com/microsoft/vcpkg.git
    GIT_TAG 66c0373dc7fca549e5803087b9487edfe3aca0a1 # 2026.01.16
  )
  FetchContent_MakeAvailable(vcpkg)
  message(STATUS "Fetched vcpkg at ${vcpkg_SOURCE_DIR}")
  execute_process(
    COMMAND chmod +x bootstrap-vcpkg.sh
    COMMAND zsh bootstrap-vcpkg.sh
    WORKING_DIRECTORY "${vcpkg_SOURCE_DIR}"
    COMMAND_ECHO STDOUT
    COMMAND_ERROR_IS_FATAL ANY
  )
endif()

# should declare set_option_custom?
set(ENV{VCPKG_ROOT} "${vcpkg_SOURCE_DIR}")
set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "toolchain")
set(VCPKG_COMMAND "$ENV{VCPKG_ROOT}/scripts/vcpkg" CACHE STRING "vcpkg executable")
# VCKPG_TARGET_TRIPLET should be automatically set to arm64-osx

# C++ basic configuration (should use set option custom)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if (NOT CMAKE_BUILD_TYPE) # TODO: And not multi config generator
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "build type")
endif ()

# after CMAKE_TOOLCHAIN Ready, add project and enable languages
project(AvkExercise NONE)

# enable needed languages
enable_language(CXX)

# find dependencies
## Volk: target_link_libraries(main PRIVATE volk::volk volk::volk_headers)
find_package(volk CONFIG REQUIRED)
## GLSL/HLSL SPIR-V Compiler frontend
find_package(glslang CONFIG REQUIRED)
# target_link_libraries(main PRIVATE glslang::glslang glslang::glslang-default-resource-limits glslang::SPIRV glslang::SPVRemapper)
## Vulkan SDK components: How should I Set the VULKAN_SDK variable?
### https://cmake.org/cmake/help/latest/module/FindVulkan.html
### target_link_libraris(my_targegt PRIVATE Vulkan::dxc)
# find_package(Vulkan REQUIRED) ## need to find DXC to compile shaders into custom targets
# message(STATUS "Vulkan found ${Vulkan_FOUND} at: ")
## VMA
find_package(VulkanMemoryAllocator CONFIG REQUIRED)

## SpirV reflect
### How'd you find this? <vcpkg-root>/installed/<triplet>/share/unofficial-spirv-reflect/
find_package(unofficial-spirv-reflect CONFIG REQUIRED)

# apple specific: find vulkan libs
## TODO: If VULKAN_SDK detected, prefer developer's local installation. otherwise
##  use vcpkg downloaded + manually fetched stuff. The result in the build-tree should
##  be the same.
if (APPLE)
  # TODO: enumerate files
  set(DYLIB_FILES
    "${CMAKE_BINARY_DIR}/vulkan/icd.d/MoltenVK_icd.json"
    "${CMAKE_BINARY_DIR}/vulkan/lib/libMoltenVK.dylib"
    "${CMAKE_BINARY_DIR}/vulkan/lib/libVkLayer_khronos_validation.dylib"
    "${CMAKE_BINARY_DIR}/vulkan/lib/libvulkan.dylib")
  set(DOWNLOAD_DYLIB OFF)
  foreach (f ${DYLIB_FILES})
    if (NOT EXISTS "${f}")
      set(DOWNLOAD_DYLIB ON)
    endif ()
  endforeach ()

  # assuming macos and not ios. If ios support needed, switch on the target triplet
  # assuming VulkanSDK Locally installed. If not, manual download of moltenVk needed
  if (DOWNLOAD_DYLIB)
    execute_process(
      COMMAND zsh "${CMAKE_SOURCE_DIR}/prepare_build_vulkan_macos.sh" "${CMAKE_BINARY_DIR}"
      COMMAND_ECHO STDOUT
      COMMAND_ERROR_IS_FATAL ANY
    )
  else ()
    message(STATUS "[MacOS Vk Build] Detected Vulkan library files from previous execution. Skipping MoltenVK Download")
  endif ()
endif ()

# add copy shader targets
function (avk_add_copy_shader target)
  # cmake_parse_arguments is built-in from 3.5, before was in module CMakeParseArguments
  cmake_parse_arguments(PARSE_ARGV 0 arg "" "" "SHADERS")
  if (TARGET ${target})
    add_custom_target(${target}-copy_shaders ALL
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/shaders"
      COMMAND ${CMAKE_COMMAND} -E copy ${arg_SHADERS} "$<TARGET_FILE_DIR:${target}>/shaders"
      USES_TERMINAL # uses the console pool on ninja generator
    )
    add_dependencies(${target} ${target}-copy_shaders)
  else ()
    message(FATAL_ERROR "TARGET ${target} doesn't exist")
  endif ()
endfunction ()

# add exercises
add_executable(avkex-saxpy)
set_target_properties(avkex-saxpy PROPERTIES 
  POSITION_INDEPENDENT_CODE ON
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
target_sources(avkex-saxpy PRIVATE 
  main.cpp avkex-instance.cpp avkex-device.cpp 
  avkex-functions.cpp avkex-commandbuffers.cpp
  avkex-discardpool.cpp avkex-os.cpp
  avkex-pipelines.cpp avkex-shader.cpp
)
target_include_directories(avkex-saxpy PRIVATE 
  "${CMAKE_CURRENT_SOURCE_DIR}"
  ## fix relative includes from vcpkg stuff, why isn't it automatic?
  "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}"
)
target_link_libraries(avkex-saxpy PRIVATE volk::volk_headers GPUOpen::VulkanMemoryAllocator unofficial::spirv-reflect)
avk_add_copy_shader(avkex-saxpy SHADERS
  "${CMAKE_SOURCE_DIR}/shaders/saxpy.first.spv"
)

set(AVKEX_SAXPY_DEFINES "")
if (CMAKE_BUILD_TYPE STREQUAL Debug)
  list(APPEND AVKEX_SAXPY_DEFINES "AVK_DEBUG" "AVK_VVL")
endif ()
if (AVK_VVL) # Note: If you define VK_LAYER_PATH, you can skip this
  list(APPEND AVKEX_SAXPY_DEFINED "AVK_VVL")
  cmake_path(GET AVK_VVL FILENAME AVK_VVL_FILENAME)
  ## add_custom_command(TARGET avkex-saxpy POST_BUILD
  ##   COMMAND ${CMAKE_COMMAND} -E create_symlink "${AVK_VVL}" "$<TARGET_FILE_DIR:avkex-saxpy>/${AVK_VVL_FILENAME}"
  ## )
endif ()

if (APPLE)
  set(AVK_LAUNCHER_SCRIPT "$<TARGET_FILE_DIR:avkex-saxpy>/run-avkex-saxpy.sh")

  add_custom_command(TARGET avkex-saxpy POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${DYLIB_FILES} $<TARGET_FILE_DIR:avkex-saxpy>
    COMMAND ${CMAKE_COMMAND} -DAVK_TARGET_FILE=$<TARGET_FILE:avkex-saxpy> -DOUTPUT_FILE="${AVK_LAUNCHER_SCRIPT}" -P "${CMAKE_CURRENT_SOURCE_DIR}/generate_launcher.cmake"
    COMMENT "Prepare for vulkan app execution"
  )

  if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_custom_command(TARGET avkex-saxpy POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:avkex-saxpy>/explicit_layer.d"
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/vulkan/explicit_layer.d" "$<TARGET_FILE_DIR:avkex-saxpy>/explicit_layer.d"
      COMMENT "Copy Explicit layer configuration"
    )
  endif ()

  # dyld (dlopen) searches dylibs into @rpath,@loader_path,absolute paths, and env
  # so set RPATH (inspect the load commands with otool -l to see RPATH)
  set_target_properties(avkex-saxpy PROPERTIES
    BUILD_RPATH "@loader_path"
    INSTALL_RPATH "@loader_path"
  )
endif ()

if (AVKEX_SAXPY_DEFINES)
  target_compile_definitions(avkex-saxpy PRIVATE ${AVKEX_SAXPY_DEFINES})
endif()
