#include "ShelfStatus.h"

#include <I18n.h>

#include "FinishedBooksStore.h"
#include "ReadingQueueStore.h"

shelfstatus::Tag shelfstatus::forPath(const std::string& path) {
  if (path.empty()) return Tag::None;
  bool rereading = false;
  bool read = false;
  for (const FinishedBook& book : FINISHED_BOOKS.getBooks()) {
    if (book.path != path) continue;
    rereading = book.rereading;
    read = book.timesRead > 0;
    break;
  }
  return classify(rereading, read, READING_QUEUE.contains(path));
}

const char* shelfstatus::label(const Tag tag) {
  switch (tag) {
    case Tag::Queued:
      return tr(STR_SHELF_TAG_QUEUED);
    case Tag::Read:
      return tr(STR_SHELF_TAG_READ);
    case Tag::Rereading:
      return tr(STR_SHELF_TAG_REREADING);
    case Tag::None:
      break;
  }
  return nullptr;
}
