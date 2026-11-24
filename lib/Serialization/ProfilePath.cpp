#include "ProfilePath.h"

#include <HalStorage.h>

#include <cstdio>
#include <cstring>

namespace profilePath {
namespace {
constexpr char ROOT[] = "/.crosspoint";
constexpr char PROFILES_DIR[] = "/.crosspoint/profiles";

// Plain uint8_t rather than an atomic: it is written from the settings screen
// and read on the paths that save and load, all on the main task. The render
// task never resolves a path.
uint8_t active = DEFAULT_ID;
}  // namespace

void setActiveId(const uint8_t id) { active = id; }

uint8_t activeId() { return active; }

std::string activeDir(const bool perProfile) {
  if (!perProfile || active == DEFAULT_ID) return std::string(ROOT) + "/";
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "/%u/", static_cast<unsigned>(active));
  return std::string(PROFILES_DIR) + buffer;
}

std::string resolve(const char* fileName, const bool perProfile) {
  if (fileName == nullptr) return {};
  // Stores hand over the absolute path they have always used, so the historical
  // "/.crosspoint/" is dropped before the profile's own directory is prepended.
  // Tolerating it here is what keeps the change to a per-profile store down to
  // the one line that opts in.
  constexpr size_t ROOT_LENGTH = sizeof(ROOT) - 1;
  if (std::strncmp(fileName, ROOT, ROOT_LENGTH) == 0) fileName += ROOT_LENGTH;
  while (*fileName == '/') ++fileName;
  return activeDir(perProfile) + fileName;
}

bool ensureActiveDir(const bool perProfile) {
  Storage.mkdir(ROOT);
  if (!perProfile || active == DEFAULT_ID) return true;
  std::string dir = activeDir(perProfile);
  // Drop the trailing slash: mkdir wants the directory, not a path inside it.
  if (!dir.empty() && dir.back() == '/') dir.pop_back();
  // pFlag defaults to true, so the profiles/ level is created with it.
  Storage.mkdir(dir.c_str());
  return Storage.exists(dir.c_str());
}

}  // namespace profilePath
