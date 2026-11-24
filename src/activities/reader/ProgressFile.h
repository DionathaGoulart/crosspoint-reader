#pragma once

#include <HalStorage.h>
#include <Logging.h>
#include <ProfilePath.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace ProgressFile {

// Name of the progress file for the reader who is using the device.
//
// Only this one file inside the book's cache directory is per profile. The rest
// of that directory -- the parsed layout, the cover, the thumbnails -- is the
// same work whoever asks for it, and is expensive enough that splitting it
// would make every profile reparse every book. The default profile keeps the
// historical name, so an existing card needs nothing moved.
inline std::string name() {
  if (profilePath::activeId() == profilePath::DEFAULT_ID) return "progress.bin";
  return "progress_" + std::to_string(static_cast<unsigned>(profilePath::activeId())) + ".bin";
}

// Absolute path of that file inside a book's cache directory.
inline std::string pathIn(const std::string& cachePath) { return cachePath + "/" + name(); }

// Writes `len` bytes of reader progress to `<cachePath>/progress.bin` without
// ever leaving the canonical file half-written.
//
// The bytes go to a temporary `progress.bin.tmp` first; only once that is fully
// written and closed is it renamed over progress.bin. An interrupted write
// (power loss or a crash mid-SPI) therefore damages only the throwaway temp file.
// Previously a truncate-in-place write that was cut short left progress.bin with
// a broken FAT cluster chain that the firmware could neither rewrite nor clear,
// stranding the book on an old page (issue #2275).
//
// This is crash-safe, not metadata-atomic: on FAT the replace is remove + rename,
// two separate directory operations, so a crash between them can leave neither
// file -- which simply reads as "no saved progress" on next launch, never a
// corrupt or unclearable file. The point is that progress.bin is never torn.
//
// Note: this prevents corruption on a healthy card going forward. It cannot
// repair an already-corrupted progress.bin -- removing the stale file may itself
// fail at the FAT level, in which case recovery still requires fsck on a host.
//
// Returns true only if the new progress.bin is fully in place.
inline bool writeAtomic(const std::string& cachePath, const uint8_t* data, size_t len) {
  const std::string finalPath = pathIn(cachePath);
  // The temp file carries the profile too: two readers saving at once would
  // otherwise write one throwaway, and the loser's rename would move the
  // winner's bytes over their own progress.
  const std::string tmpPath = finalPath + ".tmp";

  {
    HalFile f;
    if (!Storage.openFileForWrite("PRG", tmpPath, f)) {
      LOG_ERR("PRG", "Could not open temp progress file for write: %s", tmpPath.c_str());
      return false;
    }
    const size_t written = f.write(data, len);
    if (written != len) {
      LOG_ERR("PRG", "Short write saving progress to %s: %u/%u bytes", tmpPath.c_str(), (unsigned)written,
              (unsigned)len);
      return false;
    }
    f.flush();
    // f (the temp file) is closed at scope exit (DESTRUCTOR_CLOSES_FILE=1) before
    // the rename below -- SdFat must not rename a path that still has an open FsFile.
  }

  // SdFat's rename does not overwrite an existing destination, so drop the old
  // canonical file first. The brief window where neither file exists reads as
  // "no saved progress" on next launch -- never a corrupt, unclearable file.
  Storage.remove(finalPath.c_str());
  if (!Storage.rename(tmpPath.c_str(), finalPath.c_str())) {
    LOG_ERR("PRG", "Failed to rename temp progress into place: %s", finalPath.c_str());
    return false;
  }
  return true;
}

}  // namespace ProgressFile
