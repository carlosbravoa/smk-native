#!/bin/sh
# Build if needed, then play.  Any extra arguments are passed through
# (see ./build-native/smk --help).
cd "$(dirname "$0")" || exit 2
# what is missing, in words, before cmake fails in its own
tools/deps.sh || exit 1
if [ ! -d build-native ]; then
    if ! cmake -S . -B build-native -DCMAKE_BUILD_TYPE=Release >build-native.log 2>&1; then
        echo "cmake could not configure the build; its output is in build-native.log" >&2
        exit 1
    fi
    rm -f build-native.log
fi
if ! cmake --build build-native -j"$(nproc 2>/dev/null || echo 2)" >build-native.log 2>&1; then
    echo "the build failed; the compiler's output is in build-native.log" >&2
    tail -20 build-native.log >&2
    exit 1
fi
rm -f build-native.log
exec ./build-native/smk "$@"
