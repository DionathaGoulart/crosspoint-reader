#include "BookThumbnail.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <Xtc.h>

#include "components/UITheme.h"

namespace {

// Only one parser lives at a time: Epub and Xtc both exceed the task stack
// budget, so each goes on the heap and is released before the next book.
bool resolveAndGenerate(const std::string& bookPath, const int height, std::string& coverBmpPath,
                        const bool generateMissing) {
  coverBmpPath.clear();

  if (FsHelpers::hasReflowableBookExtension(bookPath)) {
    auto epub = makeUniqueNoThrow<Epub>(bookPath, "/.crosspoint");
    if (!epub) {
      LOG_ERR("THUMB", "OOM: cover EPUB");
      return false;
    }
    coverBmpPath = epub->getThumbBmpPath();
    if (Storage.exists(epub->getThumbBmpPath(height).c_str())) return true;
    if (!generateMissing) return false;
    if (epub->generateThumbBmpFromSource(height)) return true;
  } else if (FsHelpers::hasXtcExtension(bookPath)) {
    auto xtc = makeUniqueNoThrow<Xtc>(bookPath, "/.crosspoint");
    if (!xtc) {
      LOG_ERR("THUMB", "OOM: cover XTC");
      return false;
    }
    coverBmpPath = xtc->getThumbBmpPath();
    if (Storage.exists(xtc->getThumbBmpPath(height).c_str())) return true;
    if (!generateMissing) return false;
    if (xtc->load() && xtc->generateThumbBmp(height)) return true;
  }

  // No cover, or generating one failed. An empty path is the signal to draw a
  // placeholder; a stale path would make the caller try to open a file that is
  // not there on every repaint.
  coverBmpPath.clear();
  return false;
}

}  // namespace

bool bookthumb::ensure(const std::string& bookPath, const int height, std::string& coverBmpPath) {
  return resolveAndGenerate(bookPath, height, coverBmpPath, true);
}

bool bookthumb::isCached(const std::string& bookPath, const int height, std::string& coverBmpPath) {
  const bool cached = resolveAndGenerate(bookPath, height, coverBmpPath, false);
  // A miss leaves no path behind. The resolver fills one before it checks the
  // card, and a caller that uses "has a path" to mean "has a thumbnail" would
  // then skip generating every book it just asked about.
  if (!cached) coverBmpPath.clear();
  return cached;
}
