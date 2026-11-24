#pragma once

#include <string>
#include <string_view>

namespace FsHelpers {
inline bool checkFileExtension(std::string_view path, const char* extension) { return path.ends_with(extension); }
inline bool hasEpubExtension(const std::string& path) { return checkFileExtension(path, ".epub"); }
inline bool hasXtcExtension(std::string_view path) {
  return checkFileExtension(path, ".xtc") || checkFileExtension(path, ".xtch");
}
// Mirrors lib/FsHelpers: every format the library indexes. Keep the set in step
// with the real one, or this test stops testing what the builder accepts.
inline bool hasBookExtension(std::string_view path) {
  return checkFileExtension(path, ".epub") || checkFileExtension(path, ".txt") || checkFileExtension(path, ".md") ||
         hasXtcExtension(path);
}
}  // namespace FsHelpers
