#include "SleepImagePaths.h"

#include <ProfilePath.h>
#include <strings.h>

#include <cstring>

namespace sleepimages {

std::string rootDir() {
  const uint8_t id = profilePath::activeId();
  return id == profilePath::DEFAULT_ID ? std::string("/") : profilePath::dirFor(id);
}

std::string path(const char* name) { return rootDir() + (name ? name : ""); }

bool ensureRootDir() {
  // The default profile's root is the SD card itself.
  if (profilePath::activeId() == profilePath::DEFAULT_ID) return true;
  return profilePath::ensureActiveDir(/*perProfile=*/true);
}

bool isSleepImagePath(const char* path) {
  if (path == nullptr) return false;
  while (path[0] == '/' && path[1] == '/') ++path;
  // Under a profile the names are the same as at the root.
  if (const char* inside = profilePath::afterProfileDir(path)) path = inside;
  // FAT names are case-insensitive. The overlay names are not listed: only the
  // plain custom images have converted copies to drop.
  const auto isIn = [path](const char* dir) {
    const size_t length = std::strlen(dir);
    return path[0] == '/' && strncasecmp(path + 1, dir, length) == 0 && path[1 + length] == '/';
  };
  return (path[0] == '/' && strcasecmp(path + 1, CUSTOM_BMP) == 0) || isIn(CUSTOM_LEGACY_DIR) || isIn(CUSTOM_DIR);
}

}  // namespace sleepimages
