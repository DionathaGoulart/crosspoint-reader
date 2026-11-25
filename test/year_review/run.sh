#!/bin/sh
# The per-year retrospective arithmetic, on the host.
#
# Not a ctest target for the same reason as test/finished_books: the
# FinishedBook header pulls in ArduinoJson from .pio/libdeps.
#
#   test/year_review/run.sh
set -eu
cd "$(dirname "$0")/../.."
build_dir="${TMPDIR:-/tmp}/crosspoint-year-review-tests"
json_dir="${ARDUINOJSON_DIR:-}"
if [ -z "$json_dir" ] || [ ! -d "$json_dir" ]; then
  json_dir="$(find .pio/libdeps -maxdepth 3 -type d -name src -path '*ArduinoJson*' 2>/dev/null | head -1)"
fi
if [ ! -d "$json_dir" ]; then
  echo "ArduinoJson not found; build once with pio or set ARDUINOJSON_DIR" >&2
  exit 1
fi
mkdir -p "$build_dir"
c++ -std=c++20 -Wall -Wextra -Werror -fno-exceptions \
  -I"$json_dir" -Itest/finished_books/stubs -Itest/library_builder/stubs -Isrc \
  test/year_review/YearReviewTest.cpp src/stats/YearReview.cpp src/stats/StatsAggregate.cpp \
  src/stats/FinishedBooksStore.cpp \
  -o "$build_dir/test"
"$build_dir/test"
