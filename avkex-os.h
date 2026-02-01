#pragma once

#include "avkex-macros.h"

#include <optional>
#include <filesystem>

namespace avkex::os {

std::optional<std::filesystem::path> getExecutableDirectory();

}

