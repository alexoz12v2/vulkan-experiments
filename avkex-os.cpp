#include "avkex-os.h"

#include <vector>

#ifdef _WIN32
#  include <Windows.h>
#elif __APPLE__
#  include <mach-o/dyld.h>
#  include <sys/param.h> // MAXPATHLEN
#elif __linux__
#  include <unistd.h>
#  include <limits.h>
#else
#  error "Which OS are thou"
#endif

using namespace avkex;

namespace {

// https://stackoverflow.com/questions/1023306/finding-current-executables-path-without-proc-self-exe/1024937#1024937
#ifdef _WIN32
std::optional<std::filesystem::path> win32_getExecutableDirectory();
#elif __APPLE__
std::optional<std::filesystem::path> macos_getExecutableDirectory();
#elif __linux
std::optional<std::filesystem::path> linux_getExecutableDirectory();
#endif

}

namespace avkex::os {

std::optional<std::filesystem::path> getExecutableDirectory() {
#ifdef _WIN32
  return win32_getExecutableDirectory();
#elif __APPLE__
  return macos_getExecutableDirectory();
#elif __linux
  return linux_getExecutableDirectory();
#endif
}

}

namespace {

#ifdef _WIN32
std::optional<std::filesystem::path> win32_getExecutableDirectory() {
  std::vector<wchar_t> str;
  str.resize(MAX_PATH);
  if (!GetModuleFileNameW(nullptr, str.data(), MAX_PATH)) {
    return std::nullopt;
  }
  
  namespace fs = std::filesystem;
  fs::path path(str.data());
  path.remove_filename();
  return path;
}
#elif __APPLE__
// maybe readlink from unistd lets us fix the symlink problem? I don't care.

// https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man3/dyld.3.html
std::optional<std::filesystem::path> macos_getExecutableDirectory() {
  std::vector<char> str(MAXPATHLEN, char(0));
  uint32_t bufferSize = static_cast<uint32_t>(str.size());
  while (_NSGetExecutablePath(str.data(), &bufferSize) == -1) {
    str.resize(bufferSize);
  }
  // might be a symlink!
  namespace fs = std::filesystem;
  fs::path path(str.data());
  path.remove_filename();
  return path;
}
#elif __linux
// /proc/self/exe is a link to the current executable
std::optional<std::filesystem::path> linux_getExecutableDirectory() {
  std::vector<char> buf(PATH_MAX, char(0));
  ssize_t const len = readlink("/proc/self/exe", buf.data(), buf.size() - 1);
  if (len == -1) {
    return std::nullopt;
  }
  buf[len] = '\0';
  namespace fs = std::filesystem;
  fs::path path(buf.data());
  path.remove_filename();
  return path;
}
#endif

}
