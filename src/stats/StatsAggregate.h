#pragma once

#include <cstddef>
#include <cstdint>

// Reading statistics, folded out of /.crosspoint/reading_sessions.jsonl.
//
// The log is append-only and never trimmed (see src/sync/ReadingSessionLog.h),
// so after a few years it is too big to re-read every time a screen opens.
// This is the cheap summary: one pass over the lines the aggregate has not
// consumed yet, then every number a stats screen needs is a field read.
//
// Pure by design -- no HalStorage, no Logging, no Arduino. StatsRollup owns the
// file I/O; everything here is testable on the host, which is where statistics
// actually break (clocks, time zones, days that straddle midnight).
namespace stats {

// Bumped whenever the layout below changes. A mismatch makes StatsRollup
// rebuild from the log rather than read a struct it would misinterpret.
inline constexpr uint32_t kFormatVersion = 1;

// Daily minutes are a ring over the most recent kDayCount days. All-time
// figures (total seconds, session count, days read) are scalars folded
// forward, so they are not limited by this window -- but a streak longer than
// the window cannot be represented, and the month map only reaches this far
// back. Two years is well past what any screen shows.
inline constexpr int kDayCount = 730;
inline constexpr int kHourCount = 24;

// Per-book slots. At 40 bytes each this is 20 KB, which is the bulk of the
// aggregate. The X4 Classic has 8 MB of PSRAM; the ESP32-C3 baseline has about
// 380 KB of RAM, where 20 KB is a real but affordable 5%. Lower this first if
// the aggregate ever has to fit somewhere tighter.
inline constexpr int kBookCount = 512;

// The same floors readingSessionLog::upload() applies, for the same reasons: a
// session dated before 2000 was measured with an unset clock and cannot be
// placed on any day, and the duration bounds reject nonsense a crash could
// leave behind. Rejected lines change nothing, not even the totals -- counting
// time that can never be shown anywhere is worse than dropping it.
inline constexpr int64_t kMinStart = 946684800;  // 2000-01-01
inline constexpr uint32_t kMinSeconds = 10;
inline constexpr uint32_t kMaxSeconds = 7 * 86400;

struct BookStat {
  // FNV-1a of the 32-hex document id. Zero marks an empty slot.
  uint64_t docHash = 0;
  int64_t firstStart = 0;
  int64_t lastStart = 0;
  uint32_t secs = 0;
  uint32_t sessions = 0;
  // Byte offset of this book's most recent line in the log. The aggregate
  // holds no titles -- a screen that needs one seeks here and reads a single
  // line, which keeps 512 books at 20 KB instead of 44 KB.
  uint32_t lastLineOffset = 0;
  // Furthest progress ever reached, in basis points (0..10000).
  uint16_t maxTo = 0;
  uint16_t reserved = 0;
};

struct StatsAggregate {
  uint32_t version = kFormatVersion;
  uint32_t reserved0 = 0;
  // How much of the log is already folded in. The next update reads from here.
  uint64_t consumedBytes = 0;

  uint64_t totalSecs = 0;
  uint32_t sessionCount = 0;
  uint32_t longestSessionSecs = 0;
  // Distinct local days with at least one accepted session, all time.
  uint32_t daysRead = 0;
  uint32_t longestStreakDays = 0;

  int64_t firstStart = 0;
  int64_t lastStart = 0;

  // Local-day index (days since the epoch) of the newest day in the ring.
  // INT32_MIN while the ring is empty.
  int32_t lastDay = 0;
  uint32_t bookCount = 0;

  uint16_t minutesPerDay[kDayCount] = {};
  uint32_t hourHistogram[kHourCount] = {};
  BookStat books[kBookCount] = {};
};

// Days since the Unix epoch in the reader's own local time, from the session's
// recorded `tz`. A session begun at 23:50 belongs to the day the reader lived,
// not to the UTC day it may have crossed into. Floors toward negative infinity
// so pre-1970 inputs cannot land on the wrong side of zero.
int32_t localDayIndex(int64_t startEpoch, int tzMinutes);

// Local hour, 0..23, from the same offset.
int localHour(int64_t startEpoch, int tzMinutes);

void reset(StatsAggregate& aggregate);

// Folds one log line. `offset` is where the line starts in the log, recorded so
// a screen can come back for the title. Returns false when the line is not a
// usable session (unparseable, bad document id, outside the floors above);
// a false return is normal and not an error.
bool foldLine(StatsAggregate& aggregate, const char* line, size_t length, uint64_t offset);

// --- queries -----------------------------------------------------------------
//
// `today` is a local-day index, which the caller derives from the current clock
// and offset; the aggregate never reads a clock of its own.

// Minutes recorded on one day, or 0 when the day is outside the ring.
uint16_t minutesOnDay(const StatsAggregate& aggregate, int32_t day);

// Days read in the last `days` days, counting `today` backwards.
uint32_t daysReadInWindow(const StatsAggregate& aggregate, int32_t today, int days);

// Minutes in the last `days` days, counting `today` backwards.
uint32_t minutesInWindow(const StatsAggregate& aggregate, int32_t today, int days);

// Consecutive days with reading, ending today or yesterday. Yesterday still
// counts so the streak does not appear broken before the day's first session.
uint32_t currentStreak(const StatsAggregate& aggregate, int32_t today);

// The slot for a document id, or nullptr when it has no sessions.
const BookStat* findBook(const StatsAggregate& aggregate, const char* document);

uint64_t documentHash(const char* document);

}  // namespace stats
