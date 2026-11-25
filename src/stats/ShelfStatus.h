#pragma once

#include <cstdint>
#include <string>

// Where a book sits on the reader's shelf: queued to read, read, or being
// reread. Drives the library tabs and the small tag drawn on a cover.
//
// Matched by path. FinishedBooksStore is keyed by document id, but a binary
// document id reads the whole book, which is too slow for a shelf of covers;
// the store keeps the path of the last read, so a path match is enough for a
// book that has not moved on the card.
namespace shelfstatus {

enum class Tag : uint8_t { None, Queued, Read, Rereading };

// The one precedence rule: a reread beats read, and read beats queued.
constexpr Tag classify(const bool rereading, const bool read, const bool queued) {
  return rereading ? Tag::Rereading : read ? Tag::Read : queued ? Tag::Queued : Tag::None;
}

// Reads the stores as they are in memory: the caller loads them first, once,
// so an unreadable file is not parsed again for every book. Linear in the
// finished and queued books.
Tag forPath(const std::string& path);

// The short label drawn on a cover, or nullptr for Tag::None.
const char* label(Tag tag);

}  // namespace shelfstatus
