#!/bin/sh
# The reading-statistics aggregator, on the host.
#
# StatsAggregate is deliberately free of HalStorage, Logging and Arduino so it
# can be driven straight from a test: statistics break on clocks, time zones
# and days that straddle midnight, and none of those need hardware to go wrong.
# StatsRollup owns the file I/O and is exercised on the device instead.
#
#   test/stats_rollup/run.sh
set -eu
cd "$(dirname "$0")/../.."
build_dir="${TMPDIR:-/tmp}/crosspoint-stats-rollup-tests"
json_dir="${ARDUINOJSON_DIR:-.pio/libdeps/x4c-gh_release/ArduinoJson/src}"
if [ ! -d "$json_dir" ]; then
  json_dir="$(find .pio/libdeps -maxdepth 3 -type d -name src -path '*ArduinoJson*' 2>/dev/null | head -1)"
fi
if [ ! -d "$json_dir" ]; then
  echo "ArduinoJson not found; build once with pio or set ARDUINOJSON_DIR" >&2
  exit 1
fi
mkdir -p "$build_dir"
c++ -std=c++20 -Wall -Wextra -Werror -fno-exceptions \
  -I"$json_dir" -Isrc \
  test/stats_rollup/StatsAggregateTest.cpp src/stats/StatsAggregate.cpp src/stats/StatsView.cpp src/stats/Achievements.cpp \
  -o "$build_dir/test"
"$build_dir/test"
