#pragma once

// Estimates how long the rest of a chapter takes from the reader's recent page
// turns. Nothing is stored: the pace is forgotten at reboot, which keeps a slow
// day from skewing the next one.
namespace readingpace {

// Call each time the reader draws a page. A redraw of the same page, a jump, or
// a chapter change records no sample.
void notePage(int page, int pageCount);

// Whole minutes (rounded up, at least 1) to read `pagesLeft` pages, or -1 while
// there are too few samples to say.
int minutesLeft(int pagesLeft);

}  // namespace readingpace
