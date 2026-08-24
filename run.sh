#!/bin/sh
# Build if needed, then play.  Any extra arguments are passed through
# (see ./build-native/smk --help).
set -e
cd "$(dirname "$0")"
[ -d build-native ] || cmake -S . -B build-native -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build-native -j"$(nproc)" >/dev/null
exec ./build-native/smk "$@"
