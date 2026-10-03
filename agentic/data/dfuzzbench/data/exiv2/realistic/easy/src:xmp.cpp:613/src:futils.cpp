// SPDX-License-Identifier: GPL-2.0-or-later

// included header files
#include "futils.hpp"

#include "config.h"
#include "enforce.hpp"
#include "error.hpp"
#include "utils.hpp"

// + standard includes
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <stdexcept>

#ifdef EXV_ENABLE_FILESYSTEM
#if __has_include(<filesystem>)
#include <filesystem>
namespace fs = std::filesystem;
#else
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#endif
#endif

#if defined(_WIN32)
// clang-format off
#include <windows.h>
#include <psapi.h>  // For access to GetModuleFileNameEx
// clang-format on
#endif

#if __has_include(<libproc.h>)
#include <libproc.h>
#endif

#if __has_include(<unistd.h>)
#include <unistd.h>  // for getpid()
#endif

#if __has_include(<mach-o/dyld.h>)
#include <mach-o/dyld.h>  // for _NSGetExecutablePath()
#endif

#if defined(__FreeBSD__)
// clang-format off
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/un.h>
#include <libprocstat.h>
// clang-format on
#endif

#ifndef _MAX_PATH
#define _MAX_PATH 1024
#endif

namespace Exiv2 {
constexpr std::array<const char*, 2> ENVARDEF{
    "/exiv2.php",
    "40",
};  /// @brief default URL for http exiv2 handler and time-out
constexpr std::array<const char*, 2> ENVARKEY{
    "EXIV2_HTTP_POST",
    "EXIV2_TIMEOUT",
};  /// @brief request keys for http exiv2 handler and time-out

// *****************************************************************************
// free functions
std::string getEnv(int env_var) {
  // this check is relying on undefined behavior and might not be effective
  if (env_var < envHTTPPOST || env_var > envTIMEOUT) {
    throw std::out_of_range("Unexpected env variable");
  }
  return getenv(ENVARKEY[env_var]) ? getenv(ENVARKEY[env_var]) : ENVARDEF[env_var];
}

/// @brief Convert an integer value to its hex character.
static char to_hex(char code) {
  static const char hex[] = "0123456789abcdef";
  return hex[code & 15];
}

/// @brief Convert a hex character to its integer value.
static char from_hex(char ch) {
  return 0xF & (isdigit(ch) ? ch - '0' : static_cast<char>(tolower(ch)) - 'a' + 10);
}

std::string urlencode(const std::string& str) {
  std::string encoded;
  encoded.reserve(str.size() * 3);
  for (uint8_t c : str) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
      encoded += c;
    else if (c == ' ')
      encoded += '+';
    else {
      encoded += '%';
      encoded += to_hex(c >> 4);
      encoded += to_hex(c & 15);
    }
  }
  encoded.shrink_to_fit();
  return encoded;
}

void urldecode(std::string& str) {
  size_t idxIn{0};
  size_t idxOut{0};
  size_t sizeStr = str.size();
  while (idxIn < sizeStr) {
    if (str[idxIn] == '%') {
      if (str[idxIn + 1] && str[idxIn + 2]) {
        str[idxOut++] = from_hex(str[idxIn + 1]) << 4 | from_hex(str[idxIn + 2]);
        idxIn += 2;
      }
    } else if (str[idxIn] == '+') {
      str[idxOut++] = ' ';
    } else {
