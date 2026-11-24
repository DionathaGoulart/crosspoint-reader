#include "StatsRollup.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <ProfilePath.h>

#include <algorithm>
#include <cstring>

namespace {

// Per profile: the sessions this reader logged.
// The directory is ensured here rather than at each writer, because every one
// of these paths is about to touch the card anyway and a mkdir on a directory
// that exists is cheaper than a write that fails.
std::string logPath() {
  profilePath::ensureActiveDir(/*perProfile=*/true);
  return profilePath::resolve("reading_sessions.jsonl", /*perProfile=*/true);
}
// Per profile: the rollup of this reader's sessions.
// The directory is ensured here rather than at each writer, because every one
// of these paths is about to touch the card anyway and a mkdir on a directory
// that exists is cheaper than a write that fails.
std::string cacheFilePath() {
  profilePath::ensureActiveDir(/*perProfile=*/true);
  return profilePath::resolve("stats.bin", /*perProfile=*/true);
}
// readingSessionLog::append() caps a line at 512 bytes plus its newline.
constexpr size_t MAX_LINE = 512;

// Reads the line starting at `offset`, without its newline, and moves `offset`
// past it. An overlong line comes back empty but is still stepped over, so one
// bad line cannot stall the fold. Returns false, leaving `offset` alone, when
// no complete line remains: the end of the log, an unfinished last line (a
// power cut mid-append), or a read error.
//
// Same shape as readLine() in ReadingSessionLog.cpp, and deliberately a copy:
// that one is file-local to the uploader, and sharing it would mean a header
// in src/sync/ that both the uploader and this cache depend on.
bool readLine(HalFile& file, size_t& offset, char* line, const size_t size) {
  size_t next = offset;
  bool overlong = false;
  while (file.seek(next)) {
    const int got = file.read(line, size - 1);
    if (got <= 0) return false;
    if (const void* newline = memchr(line, '\n', got)) {
      const size_t length = static_cast<const char*>(newline) - line;
      line[overlong ? 0 : length] = '\0';
      offset = next + length + 1;
      return true;
    }
    if (static_cast<size_t>(got) < size - 1) return false;
    overlong = true;
    next += got;
  }
  return false;
}

bool readCache(stats::StatsAggregate& out) {
  HalFile file;
  if (!Storage.openFileForRead("STATS", cacheFilePath().c_str(), file)) return false;
  const bool sized = file.size() == sizeof(stats::StatsAggregate);
  const bool read = sized && file.read(&out, sizeof(out)) == static_cast<int>(sizeof(out));
  file.close();
  if (!read) return false;
  if (out.version != stats::kFormatVersion) {
    LOG_INF("STATS", "stats.bin is version %u, rebuilding for %u", static_cast<unsigned>(out.version),
            static_cast<unsigned>(stats::kFormatVersion));
    return false;
  }
  return true;
}

// Written through a temp file and renamed, like ProgressFile::writeAtomic: a
// cut-short write damages only the throwaway, and a half-written stats.bin
// would read as a plausible struct full of nonsense.
bool writeCache(const stats::StatsAggregate& aggregate) {
  // Beside the file it replaces, not in the shared root: replaceFile() is a
  // rename, which FAT will not do across directories, and two profiles
  // writing one temp file would corrupt each other's rollup.
  const std::string tmp = profilePath::resolve("stats.bin.tmp", /*perProfile=*/true);
  const char* tmpPath = tmp.c_str();
  Storage.mkdir("/.crosspoint");
  bool ok = false;
  {
    HalFile file;
    if (!Storage.openFileForWrite("STATS", tmpPath, file)) return false;
    ok = file.write(&aggregate, sizeof(aggregate)) == sizeof(aggregate);
    ok = file.close() && ok;
  }
  if (!ok) {
    Storage.remove(tmpPath);
    return false;
  }
  // SdFat's rename does not overwrite, so the old file goes first. The gap
  // where neither exists reads as "no cache", which only costs a rebuild.
  Storage.remove(cacheFilePath().c_str());
  if (!Storage.rename(tmpPath, cacheFilePath().c_str())) {
    Storage.remove(tmpPath);
    return false;
  }
  return true;
}

}  // namespace

std::string statsRollup::cachePath() { return cacheFilePath(); }

void statsRollup::invalidate() { Storage.remove(cacheFilePath().c_str()); }

bool statsRollup::load(stats::StatsAggregate& out) {
  stats::reset(out);
  const bool cached = readCache(out);
  if (!cached) stats::reset(out);

  if (!Storage.exists(logPath().c_str())) {
    // No reading yet. An empty aggregate is the honest answer, and a stale
    // cache from a log that was deleted must not survive it.
    if (cached && out.consumedBytes > 0) {
      stats::reset(out);
      writeCache(out);
    }
    return true;
  }

  HalFile file;
  if (!Storage.openFileForRead("STATS", logPath().c_str(), file)) return cached;
  const uint64_t logSize = file.size();

  // A log shorter than what we folded was replaced or truncated; the only safe
  // reading is from the start.
  if (out.consumedBytes > logSize) {
    LOG_INF("STATS", "session log shrank, rebuilding statistics");
    stats::reset(out);
  }
  if (out.consumedBytes == logSize) {
    file.close();
    return true;
  }

  const auto buffer = makeUniqueNoThrow<char[]>(MAX_LINE + 1);
  if (!buffer) {
    file.close();
    LOG_ERR("STATS", "no memory for the session line buffer");
    return cached;
  }

  size_t offset = static_cast<size_t>(out.consumedBytes);
  size_t folded = 0;
  while (true) {
    const size_t lineStart = offset;
    if (!readLine(file, offset, buffer.get(), MAX_LINE + 1)) break;
    if (stats::foldLine(out, buffer.get(), strlen(buffer.get()), lineStart)) ++folded;
    out.consumedBytes = offset;
  }
  file.close();

  if (folded > 0 || !cached) {
    LOG_DBG("STATS", "folded %u session(s); %u total, %llu seconds", static_cast<unsigned>(folded),
            static_cast<unsigned>(out.sessionCount), static_cast<unsigned long long>(out.totalSecs));
    writeCache(out);
  }
  return true;
}

bool statsRollup::appendLine(const char* json, const size_t jsonLength, const uint64_t lineStart,
                             const uint64_t logSizeAfterAppend) {
  if (json == nullptr || jsonLength == 0 || lineStart > logSizeAfterAppend) return false;

  // On the heap, never the stack: StatsAggregate is ~22 KB, and this runs on
  // the task that closed the book. No FreeRTOS stack here is anywhere near
  // that size, so a local would smash it.
  const auto held = makeUniqueNoThrow<stats::StatsAggregate>();
  if (!held) return false;
  stats::StatsAggregate& aggregate = *held;

  stats::reset(aggregate);
  if (!readCache(aggregate)) return false;

  // The line has to start exactly where folding stopped. Anything else means
  // the log grew behind our back, and only a full pass can catch up -- which
  // load() will do, because returning here leaves consumedBytes where it was.
  if (aggregate.consumedBytes != lineStart) return false;

  stats::foldLine(aggregate, json, jsonLength, lineStart);
  aggregate.consumedBytes = logSizeAfterAppend;
  return writeCache(aggregate);
}
