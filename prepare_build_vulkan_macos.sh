
set -euo pipefail

# ---------------------------------------------------------------------
# Target Build Tree to create: (ios/macos, x64/arm64)
# $BUILDDIR/
# └─ vulkan/
#    ├─ icd.d/
#    │  └─ MoltenVK_icd.json
#    ├─ explicit_layer.d/
#    ┃  └─ VkLayer_khronos_validation.json (copied to target_file_dir/explicit_layer.d)
#    └─ lib/
#       ├─ libMoltenVK.dylib
#       ├─ libVkLayer_khronos_validation.dylib (copied, but only used in debug)
#       └─ libvulkan.dylib (symlinks omitted) 
# ---------------------------------------------------------------------

SCRIPT="$(cd "$(dirname "$0")" && pwd)/$(basename "$0")"
SCRIPTDIR="$(cd "$(dirname "$SCRIPT")" && pwd)"
BUILDDIR="$1"

# import utils
source "$SCRIPTDIR/sh_utils.sh"

print_info "[MacOS Vk Build]" " Starting script for organizing local vulkan libs"
echo "  BUILDDIR:   ${BUILDDIR}"
echo "  VULKAN_SDK: ${VULKAN_SDK}"

if [[ ! -d "$BUILDDIR" ]]; then
  print_err "[MacOS Vk Build]" " $BUILDDIR is not a directory"
  exit 1
fi

# TODO: implement caching behaviour, ie if structure exists, skip next

# C.1. Common: Create output directory structure
mkdir -p "$BUILDDIR"/vulkan/{icd.d,lib,explicit_layer.d}
print_info "[MacOS Vk Build]" " Created Directory structure at $BUILDDIR"

# ---------------------------------------------------------------------
# Download Mode: 
# - Assuming BUILDDIR already has vcpkg_installed/: grab loader and vvl
#   - assumes major for SDK is 1.4 (until it breaks) 
# - Download MoltenVk from releases on github, decompress tar
# ---------------------------------------------------------------------
copy_vk_libraries() {
  local TRIPLET="arm64-osx" # TODO customizable
  local DESTDIR="$BUILDDIR/vulkan/lib"
  local LIBDIR="$BUILDDIR/vcpkg_installed/$TRIPLET/lib"
  if [[ ! -d "$LIBDIR" ]]; then
    print_err "[MacOS Vk Build]" " $LIBDIR doesn't exist"
    exit 1
  fi
  # take all dylibs (which should be rwxr-xr-x files), look over and copy
  # TODO: more specific reg for vulkan only dylib (now we don't have other deps)
  print_info "[MacOS Vk Build]" " Ready to copy files from $LIBDIR"
  for SRC in "$LIBDIR/"*.dylib; do
    [[ -e "$SRC" ]] || continue
    local filename="$(basename "$SRC")"
    local DST="$DESTDIR/$filename"
    print_info "[MacOS Vk Build]" " - \"$SRC\" -> \"$DST\""
    cp -P "$SRC" "$DST"
  done

  for SRC in "$BUILDDIR/vcpkg_installed/$TRIPLET/share/vulkan/explicit_layer.d/"*.json; do
    [[ -e "$SRC" ]] || continue
    local filename="$(basename "$SRC")"
    local DST="$BUILDDIR/vulkan/explicit_layer.d/$filename"
    print_info "[MacOS Vk Build]" " - \"$SRC\" -> \"$DST\""
    cp -P "$SRC" "$DST"
  done
}

download_moltenvk() {
  local FILEURL='https://github.com/KhronosGroup/MoltenVK/releases/download/v1.4.1/MoltenVK-macos.tar'
  print_info "[MacOS Vk Build]" " Downloading MoltenVK-macos.tar from $FILEURL"
  curl -fo "$BUILDDIR/MoltenVK-macos.tar" -L "$FILEURL"
  if [[ $? != "0" ]]; then
    exit 1
  fi
  print_info "[MacOS Vk Build]" " Extracting MoltenVK-macos.tar into $BUILDDIR/MoltenVK-macos/"
  mkdir -p "$BUILDDIR/MoltenVK-macos"
  # Docs/ MoltenVK/ LICENSE
  tar -xzf "$BUILDDIR/MoltenVK-macos.tar" --strip-components=1 -C "$BUILDDIR/MoltenVK-macos"
  if [[ $? != "0" ]]; then
    exit 1
  fi
  # $extracted/MoltenVK/dynamic/dylib/macOS/{libMoltenVK.dylib,MoltenVK_icd.json}
  local SRCDIR="$BUILDDIR/MoltenVK-macos/MoltenVK/dynamic/dylib/macOS"

  cp "$SRCDIR/libMoltenVK.dylib" "$BUILDDIR/vulkan/lib/libMoltenVK.dylib"
  cp "$SRCDIR/MoltenVK_icd.json" "$BUILDDIR/vulkan/icd.d/MoltenVK_icd.json"
  print_info "[MacOS Vk Build]" " Copied files libMoltenVK.dylib,MoltenVK_icd.json into $BUILDDIR/vulkan"
  # Note: Leave the ICD specified path intact, post build step will take care
  #    of it.
}

# go
copy_vk_libraries
download_moltenvk

# ---------------------------------------------------------------------
# VULKAN_SDK Mode: TODO
# ---------------------------------------------------------------------


