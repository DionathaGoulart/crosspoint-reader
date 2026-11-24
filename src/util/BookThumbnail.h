#pragma once

#include <string>

// Cover thumbnails for a book on the card, shared by every screen that shows a
// shelf of covers.
//
// Generation is the expensive half: it opens the book, finds the cover image
// and rescales it. The caller owns whatever it wants to show while that runs --
// the home screen draws a progress popup, the library a plain one -- so none of
// that UI lives here.
namespace bookthumb {

// Resolves `bookPath`'s cover BMP path into `coverBmpPath` and makes sure a
// thumbnail of `height` exists for it, generating one from the book when it is
// missing.
//
// Returns false when the book has no usable cover, leaving `coverBmpPath`
// empty, so a caller can fall back to a placeholder rather than an empty cell.
//
// A caller that wants to show a popup for the slow path probes with isCached()
// first: whether generation is due has to be known before it starts, not
// reported after it finishes.
bool ensure(const std::string& bookPath, int height, std::string& coverBmpPath);

// True when a thumbnail of `height` is already on the card, so a caller can
// check a whole page cheaply before deciding to show a popup.
bool isCached(const std::string& bookPath, int height, std::string& coverBmpPath);

}  // namespace bookthumb
