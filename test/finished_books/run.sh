#!/bin/sh
# The reread rule and the yearly-goal rule, on the host.
#
# Not a ctest target: FinishedBooksStore needs ArduinoJson, which lives under
# .pio/libdeps and is not available in the CMake test tree (nor in the CI
# unit-tests job, which installs no PlatformIO). Same reason as
# test/stats_rollup and test/sync_extensions.
#
#   test/finished_books/run.sh
set -eu
cd "$(dirname "$0")/../.."
build_dir="${TMPDIR:-/tmp}/crosspoint-finished-books-tests"
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
  test/finished_books/FinishedBooksStoreTest.cpp src/stats/FinishedBooksStore.cpp \
  -o "$build_dir/test"
"$build_dir/test"
