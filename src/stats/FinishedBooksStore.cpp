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
  struct tm local {};
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

bool FinishedBooksStore::markFinished(const std::string& document, const std::string& path, const std::string& title,
                                      const std::string& author, const std::string& language,
                                      const int64_t finishedEpoch) {
  if (document.empty()) return false;

  const auto existing =
      std::find_if(books.begin(), books.end(), [&document](const FinishedBook& b) { return b.document == document; });
  if (existing != books.end()) {
    // Refresh where it lives and what it is called, but never the date: the
    // first time the reader reached the end is the fact worth keeping.
    existing->path = path;
    if (!title.empty()) existing->title = title;
    if (!author.empty()) existing->author = author;
    if (!language.empty()) existing->language = language;
    saveToFile();
    return false;
  }

  FinishedBook book;
  book.document = document;
  book.path = path;
  book.title = title;
  book.author = author;
  book.language = language;
  book.finishedEpoch = finishedEpoch;
  books.insert(books.begin(), std::move(book));
  if (books.size() > MAX_BOOKS) books.resize(MAX_BOOKS);
  saveToFile();
  LOG_INF("FBS", "Finished book recorded (%u total)", static_cast<unsigned>(books.size()));
  return true;
}

size_t FinishedBooksStore::countInYear(const int64_t epoch) const {
  if (epoch <= 0) return 0;
  const int year = localYear(epoch);
  return static_cast<size_t>(std::count_if(books.begin(), books.end(), [year](const FinishedBook& book) {
    return book.finishedEpoch > 0 && localYear(book.finishedEpoch) == year;
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
  return FINISHED_BOOKS.markFinished(document, bookPath, title, author, language, now);
}
