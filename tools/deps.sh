#!/bin/sh
# What the port needs to build and run, checked before cmake gets a chance
# to fail in its own words.  Prints what is missing and the one command
# that installs it on this machine.  Exit 0 when everything is there.
#
#   tools/deps.sh          the game: compiler, cmake, pkg-config, SDL2, SDL2_mixer, the ROM
#   tools/deps.sh editor   also python3 with tkinter, for the course editor
cd "$(dirname "$0")/.." || exit 2

missing=""          # package names, for the install line
what=""             # what each one is, for the reader

have() { command -v "$1" >/dev/null 2>&1; }
pc() { have pkg-config && pkg-config --exists "$1" 2>/dev/null; }

# the package manager decides the names
if have apt-get; then
    pm="sudo apt install"; p_cc=build-essential; p_cmake=cmake; p_pkg=pkg-config
    p_sdl=libsdl2-dev; p_mix=libsdl2-mixer-dev; p_py=python3; p_tk=python3-tk
elif have dnf; then
    pm="sudo dnf install"; p_cc=gcc; p_cmake=cmake; p_pkg=pkgconf-pkg-config
    p_sdl=SDL2-devel; p_mix=SDL2_mixer-devel; p_py=python3; p_tk=python3-tkinter
elif have pacman; then
    pm="sudo pacman -S"; p_cc=base-devel; p_cmake=cmake; p_pkg=pkgconf
    p_sdl=sdl2; p_mix=sdl2_mixer; p_py=python; p_tk=tk
elif have zypper; then
    pm="sudo zypper install"; p_cc=gcc; p_cmake=cmake; p_pkg=pkg-config
    p_sdl=SDL2-devel; p_mix=SDL2_mixer-devel; p_py=python3; p_tk=python3-tk
elif have brew; then
    pm="brew install"; p_cc="(Xcode command line tools: xcode-select --install)"; p_cmake=cmake; p_pkg=pkg-config
    p_sdl=sdl2; p_mix=sdl2_mixer; p_py=python; p_tk=python-tk
else
    pm="(your package manager)"; p_cc="a C compiler"; p_cmake=cmake; p_pkg=pkg-config
    p_sdl="SDL2 development files"; p_mix="SDL2_mixer development files"; p_py=python3; p_tk="python3 tkinter"
fi

need() {   # need <ok?> <package> <what it is>
    if [ "$1" != 0 ]; then
        missing="$missing $2"
        what="$what
  - $3"
    fi
}

( have cc || have gcc || have clang );          need $? "$p_cc"    "a C compiler (cc)"
have cmake;                                     need $? "$p_cmake" "cmake, which drives the build"
have pkg-config;                                need $? "$p_pkg"   "pkg-config, which finds SDL"
if have pkg-config; then
    pc sdl2;                                    need $? "$p_sdl"   "SDL2 development files (window, input, audio)"
    pc SDL2_mixer;                              need $? "$p_mix"   "SDL2_mixer development files (the sound mixer)"
else
    # cannot be checked without pkg-config; a machine without it lacks these too
    need 1 "$p_sdl" "SDL2 development files (window, input, audio)"
    need 1 "$p_mix" "SDL2_mixer development files (the sound mixer)"
fi
if [ "$1" = editor ]; then
    have python3;                               need $? "$p_py"    "python3, which runs the course tools"
    if have python3; then
        python3 -c 'import tkinter' 2>/dev/null; need $? "$p_tk"  "tkinter, the editor's toolkit"
    fi
fi

status=0
if [ -n "$missing" ]; then
    echo "This machine is missing what the port needs to build:$what" >&2
    echo "" >&2
    echo "Install it with:" >&2
    echo "    $pm$missing" >&2
    echo "" >&2
    status=1
fi
if [ ! -f rom/smk_usa.sfc ]; then
    echo "The ROM is missing: copy your own Super Mario Kart (USA) ROM to rom/smk_usa.sfc" >&2
    echo "(the port reads every tile, sound and table from it at run time; none is in this repository)." >&2
    status=1
fi
exit $status
