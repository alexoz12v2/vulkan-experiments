# Vulkan Compute Exercise

## CMake Setup

- see cmake-buildsystem documentation to see how to define an executable target.
  - is `POSITION_INDEPENDENT_CODE` necessary? Yes?
- executables for macOS should be declared as MACOSX bundles.
- are toolchain files necessary? Yes probably, if you want to explicitly define which compiler you're going to
  be using or if you have special needs

### Dependency management

I want to use `vcpkg` to manage my dependencies. The strategy is the following:

- download through `FetchContent` (meaning each config will have its own vcpkg) the vcpkg repo and execute their build script
- load the vcpkg toolchain file after preparing the environment, including the manifest file
- download vcpkg dependencies for the needed triplets
- use their ports to import dependencies as cmake targets

use `vulkan-sdk-components` as your only dependency. It has everything you need except for VMA.

### Building and formatting

```shell
# configure step
cmake -S . -B cmake-build-debug -G Ninja
# dump basic style
clang-format -style=google -dump-config > .clang-format
# format
find . -regex '.*\.\(cpp\|hpp\|cc\|cxx\)' -exec clang-format -style=file -i {} \;
```

