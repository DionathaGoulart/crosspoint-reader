#pragma once

#include <cstdint>
#include <string>

#include "StatsAggregate.h"

// The file half of the reading statistics: reads /.crosspoint/stats.bin, folds
// whatever the reading session log has gained since it was written, and writes
// it back.
//
// stats.bin is a cache, never the source of truth. Deleting it costs one
// rebuild from reading_sessions.jsonl and nothing else, which is why nothing
// here ever reports a missing file as an error.
//
// Checked: Clear Reading Cache does not remove either file. ClearCacheActivity
// only deletes directories whose names match isBookCacheDirectoryName -- the
// epub_/txt_/xtc_ prefixes (src/util/BookCacheUtils.cpp) -- so loose files in
// /.crosspoint survive it.
namespace statsRollup {

// Fills `out` with current figures: reads stats.bin when it is valid, folds the
// unconsumed tail of the log, and rewrites stats.bin when anything changed.
//
// Rebuilds from byte zero when stats.bin is absent, its version does not match
// kFormatVersion, or the log is shorter than the aggregate claims to have
// consumed (the log was replaced or truncated).
//
// Returns false only when `out` could not be filled at all. No log yet is a
// success with an empty aggregate, which is the first-boot case.
//
// `out` is taken by reference because StatsAggregate is about 22 KB: the
// caller has to put it on the heap (makeUniqueNoThrow<stats::StatsAggregate>())
// and keep it alive, never declare one as a local. No task stack in this
// firmware can hold it.
bool load(stats::StatsAggregate& out);

// Folds one freshly appended line into stats.bin without re-reading the log.
// readingSessionLog::append() calls this after its own write succeeds, so the
// common path never opens the log at all.
//
// `json` is the record's text without its newline. `lineStart` is the byte
// offset it was written at, and `logSizeAfterAppend` the log's size once it is
// on disk -- both are passed in rather than inferred, because append() may
// prepend a newline to close an unfinished line left by a power cut, and
// guessing that byte would misplace every later offset.
//
// A `lineStart` that is not where the aggregate stopped folding means the log
// grew by some other path; this returns false and leaves the cache alone, so
// the next load() catches up with a full pass.
//
// Best effort: a failure here loses nothing, because load() can always rebuild.
bool appendLine(const char* json, size_t jsonLength, uint64_t lineStart, uint64_t logSizeAfterAppend);

// Drops stats.bin so the next load() rebuilds. For a settings action or a test.
void invalidate();

// Path of the cache file, exposed for tests and diagnostics. By value because
// it depends on the active profile: a const char* would dangle the moment the
// temporary it pointed into went away.
std::string cachePath();

}  // namespace statsRollup
