#!/bin/sh
# Reading goals, on the host.
#
# Goals is deliberately free of settings, storage and Arduino -- it is
# arithmetic over stats::Summary -- so it needs no ArduinoJson and no .pio
# libdeps, unlike test/stats_rollup/run.sh. Only the Summary struct is needed
# from StatsView, so its .cpp (and through it ArduinoJson) stays out of the link.
#
#   test/goals/run.sh
set -eu
cd "$(dirname "$0")/../.."
build_dir="${TMPDIR:-/tmp}/crosspoint-goals-tests"
mkdir -p "$build_dir"
c++ -std=c++20 -Wall -Wextra -Werror -fno-exceptions \
  -Isrc \
  test/goals/GoalsTest.cpp src/stats/Goals.cpp \
  -o "$build_dir/test"
"$build_dir/test"
