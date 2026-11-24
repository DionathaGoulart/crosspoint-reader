#include "ProfilePath.h"

#include <HalStorage.h>

#include <atomic>
#include <cstdio>
#include <cstring>

namespace profilePath {
namespace {
constexpr char ROOT[] = "/.crosspoint";
constexpr char PROFILES_DIR[] = "/.crosspoint/profiles";

// Atomic because this is read from more than one task, which an earlier comment
// here denied. Two readers are known: the web server task saves settings
// (CrossPointWebServer -> CrossPointSettings::saveToFile), and the render task
// resolves the progress file name on every page save
// (EpubReaderActivity::renderBook -> ProgressFile::pathIn -> activeId).
// A byte load is indivisible on both targets, so nothing tears either way; what
// the atomic buys is that a switch cannot be read half-published, and that the
// next per-profile path added on a render path does not inherit a false
// promise. Relaxed: there is no other state being published with it.
std::atomic<uint8_t> active{DEFAULT_ID};
}  // namespace

void setActiveId(const uint8_t id) { active.store(id, std::memory_order_relaxed); }

uint8_t activeId() { return active.load(std::memory_order_relaxed); }

std::string dirFor(const uint8_t id) {
  if (id == DEFAULT_ID) return std::string(ROOT) + "/";
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "/%u/", static_cast<unsigned>(id));
  return std::string(PROFILES_DIR) + buffer;
}

std::string activeDir(const bool perProfile) {
  if (!perProfile) return std::string(ROOT) + "/";
  return dirFor(activeId());
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
  // Before any mkdir: a device-wide store, and every store under the default
  // profile, must cost exactly what it cost before profiles existed.
  // writeDocToFile() already ensures the root for those.
  if (!perProfile || activeId() == DEFAULT_ID) return true;
  Storage.mkdir(ROOT);
  std::string dir = activeDir(perProfile);
  // Drop the trailing slash: mkdir wants the directory, not a path inside it.
  if (!dir.empty() && dir.back() == '/') dir.pop_back();
  // pFlag defaults to true, so the profiles/ level is created with it.
  Storage.mkdir(dir.c_str());
  return Storage.exists(dir.c_str());
}

}  // namespace profilePath
