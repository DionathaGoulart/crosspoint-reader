#include "FinishedBooksStore.h"

#include <KOReaderCredentialStore.h>
#include <KOReaderDocumentId.h>
#include <Logging.h>
#include <TrustedTime.h>

#include <algorithm>
#include <ctime>

namespace {

int localYear(const int64_t epoch) {
  const time_t at = static_cast<time_t>(epoch);
  struct tm local{};
  localtime_r(&at, &local);
  return local.tm_year + 1900;
}

}  // namespace

void FinishedBooksStore::toJson(JsonDocument& doc) const {
  JsonArray arr = doc["books"].to<JsonArray>();
  for (const auto& book : books) {
    JsonObject obj = arr.add<JsonObject>();
    obj["doc"] = book.document;
    obj["path"] = book.path;
    obj["title"] = book.title;
    obj["author"] = book.author;
    obj["lang"] = book.language;
    obj["at"] = book.finishedEpoch;
    // Always written, even at zero. Omitting it would make "no reads key" mean
    // two different things -- a file from before this field, and an entry that
    // exists only for a rating -- and fromJson() cannot tell those apart. It
    // guessed "legacy", so rating a book you had never read turned it into a
    // finished one, and the real first finish was then never counted.
    obj["reads"] = book.timesRead;
    if (book.lastFinishedEpoch != 0) obj["lastAt"] = book.lastFinishedEpoch;
    if (book.rating != 0) obj["rate"] = book.rating;
    if (book.rereading) obj["rereading"] = true;
  }
}

bool FinishedBooksStore::fromJson(JsonVariantConst doc) {
  // A missing or malformed list reads as "nothing finished yet" rather than
  // an error: the only fatal case is a JSON parse failure, which the caller
  // already handled. A null JsonArray iterates zero times.
  books.clear();
  JsonArrayConst arr = doc["books"].as<JsonArrayConst>();
  books.reserve(std::min(arr.size(), MAX_BOOKS));
  for (JsonObjectConst obj : arr) {
    if (books.size() >= MAX_BOOKS) break;
    FinishedBook book;
    book.document = obj["doc"] | "";
    book.path = obj["path"] | "";
    book.title = obj["title"] | "";
    book.author = obj["author"] | "";
    book.language = obj["lang"] | "";
    book.finishedEpoch = obj["at"] | static_cast<int64_t>(0);
    book.timesRead = obj["reads"] | static_cast<uint16_t>(0);
    book.lastFinishedEpoch = obj["lastAt"] | static_cast<int64_t>(0);
    book.rating = obj["rate"] | static_cast<int8_t>(0);
    book.rereading = obj["rereading"] | false;
    if (book.rating < -1 || book.rating > 1) book.rating = 0;
    // No reads key at all means the entry predates the field, and every entry
    // written then was a finished book: the store only ever held those.
    if (!obj["reads"].is<uint16_t>()) {
      book.timesRead = 1;
      requestResave();
    }
    if (book.lastFinishedEpoch == 0) book.lastFinishedEpoch = book.finishedEpoch;
    if (book.document.empty()) continue;
    books.push_back(std::move(book));
  }
  LOG_DBG("FBS", "Finished books loaded (%u entries)", static_cast<unsigned>(books.size()));
  return true;
}

bool FinishedBooksStore::contains(const std::string& document) const {
  return std::any_of(books.begin(), books.end(),
                     [&document](const FinishedBook& book) { return book.document == document; });
}

const FinishedBook* FinishedBooksStore::find(const std::string& document) const {
  const auto it = std::find_if(books.begin(), books.end(),
                               [&document](const FinishedBook& book) { return book.document == document; });
  return it == books.end() ? nullptr : &*it;
}

size_t FinishedBooksStore::finishedCount() const {
  return static_cast<size_t>(
      std::count_if(books.begin(), books.end(), [](const FinishedBook& book) { return book.timesRead > 0; }));
}

// Finds the entry for `document`, creating it from the book's details when it
// has none. Returns nullptr only when the document id is empty, which means the
// caller could not identify the book at all.
FinishedBook* FinishedBooksStore::entryFor(const std::string& document, const std::string& path,
                                           const std::string& title, const std::string& author,
                                           const std::string& language) {
  if (document.empty()) return nullptr;
  const auto it =
      std::find_if(books.begin(), books.end(), [&document](const FinishedBook& b) { return b.document == document; });
  if (it != books.end()) {
    // Refresh where it lives and what it is called; the dates are not touched.
    it->path = path;
    if (!title.empty()) it->title = title;
    if (!author.empty()) it->author = author;
    if (!language.empty()) it->language = language;
    return &*it;
  }
  FinishedBook book;
  book.document = document;
  book.path = path;
  book.title = title;
  book.author = author;
  book.language = language;
  books.insert(books.begin(), std::move(book));
  if (books.size() > MAX_BOOKS) books.resize(MAX_BOOKS);
  return &books.front();
}

bool FinishedBooksStore::markRead(const std::string& document, const std::string& path, const std::string& title,
                                  const std::string& author, const std::string& language, const int64_t epoch) {
  FinishedBook* book = entryFor(document, path, title, author, language);
  if (book == nullptr) return false;

  const bool firstFinish = book->timesRead == 0;
  if (!firstFinish && !book->rereading) {
    // Reaching the end of a book already read, without having asked to reread
    // it. This is someone revisiting a passage, so nothing is counted; the
    // refreshed path and title from entryFor() are still worth saving.
    saveToFile();
    return false;
  }

  ++book->timesRead;
  book->lastFinishedEpoch = epoch;
  if (firstFinish) book->finishedEpoch = epoch;
  book->rereading = false;
  saveToFile();
  LOG_INF("FBS", "Book read %u time(s) (%u finished)", static_cast<unsigned>(book->timesRead),
          static_cast<unsigned>(finishedCount()));
  return true;
}

bool FinishedBooksStore::setRereading(const std::string& document, const std::string& path, const std::string& title,
                                      const std::string& author, const std::string& language, const bool rereading) {
  FinishedBook* book = entryFor(document, path, title, author, language);
  if (book == nullptr) return false;
  book->rereading = rereading;
  return saveToFile();
}

bool FinishedBooksStore::setRating(const std::string& document, const std::string& path, const std::string& title,
                                   const std::string& author, const std::string& language, const int8_t rating) {
  if (rating < -1 || rating > 1) return false;
  FinishedBook* book = entryFor(document, path, title, author, language);
  if (book == nullptr) return false;
  book->rating = rating;
  return saveToFile();
}

size_t FinishedBooksStore::countTowardYearlyGoal(const int64_t now) const {
  if (now <= 0) return 0;
  const int year = localYear(now);
  return static_cast<size_t>(std::count_if(books.begin(), books.end(), [year](const FinishedBook& book) {
    // timesRead, not the date: an entry can exist for a rating alone, and that
    // is the test the rest of this class uses for "finished".
    if (book.timesRead == 0 || book.finishedEpoch <= 0) return false;
    // Either dated finish falling in this year is enough. Because this is a
    // per-book predicate, a book read twice inside one year still counts once,
    // while the same book reread in a later year counts again for that year.
    if (localYear(book.finishedEpoch) == year) return true;
    // Skipped when the two dates are equal, which is every book read once and
    // every entry migrated by fromJson(): localYear() would convert the same
    // epoch the line above just rejected.
    return book.lastFinishedEpoch != book.finishedEpoch && localYear(book.lastFinishedEpoch) == year;
  }));
}

std::string finishedbooks::documentIdFor(const std::string& bookPath) {
  // Mirrors ReaderActivity::flushReaderSession(): the id in the session log is
  // the binary one only when the sync match method asks for it. Using a
  // different rule here would file the same book under two keys.
  if (KOREADER_STORE.getMatchMethod() == DocumentMatchMethod::BINARY) {
    return KOReaderDocumentId::calculate(bookPath);
  }
  return KOReaderDocumentId::calculateFromFilename(bookPath);
}

bool finishedbooks::recordFinished(const std::string& bookPath, const std::string& title, const std::string& author,
                                   const std::string& language) {
  const int64_t now = trustedtime::trustedNow();
  if (now <= 0) {
    // Without a trusted clock the book cannot be dated, and an undated entry
    // would poison every per-year and per-day figure built on this store.
    LOG_DBG("FBS", "Clock not set; finished book not recorded");
    return false;
  }
  const std::string document = documentIdFor(bookPath);
  if (document.empty()) return false;
  return FINISHED_BOOKS.markRead(document, bookPath, title, author, language, now);
}
