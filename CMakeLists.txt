cmake_minimum_required(VERSION 3.16)
project(smk C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# GCC/clang spell warnings one way and MSVC another; MinGW-w64 is the
# supported Windows toolchain, so the GNU spelling is the common case.
if(MSVC)
  set(SMK_WARN /W3 /O2)
else()
  set(SMK_WARN -Wall -Wextra -O2)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

if(WIN32)
  # printf on MinGW is whichever the CRT supplies, and the older msvcrt
  # one knows nothing of C99 - no %zu, no %lld.  This asks mingw-w64 for
  # its own conforming printf on every CRT, so a format string that is
  # right by the standard is right here too.
  add_compile_definitions(__USE_MINGW_ANSI_STDIO=1)
endif()

# SDL2's pkg-config file on MinGW asks for -mwindows, which builds a GUI
# subsystem binary: started from a shell it detaches from the console and
# every line the port prints - why the ROM would not load, why there is
# no sound - goes nowhere.  Take the flag back out; SDL_main still
# supplies the entry point.
if(WIN32 AND TARGET PkgConfig::SDL2)
  get_target_property(SMK_SDL_LINKOPT PkgConfig::SDL2 INTERFACE_LINK_OPTIONS)
  if(SMK_SDL_LINKOPT)
    list(REMOVE_ITEM SMK_SDL_LINKOPT "-mwindows")
    set_target_properties(PkgConfig::SDL2 PROPERTIES INTERFACE_LINK_OPTIONS "${SMK_SDL_LINKOPT}")
  endif()
endif()

add_library(smkcore STATIC
  src/rom.c
  src/smkos.c
  src/lzc.c
  src/assets.c
  src/mode7.c
  src/kart.c
  src/player.c
  src/demolog.c
  src/effects.c
  src/coinfx.c
  src/item.c
  src/projectile.c
  src/pickup.c
  src/horizon.c
  src/blocks.c
  src/physics.c
  src/ai.c
  src/sprite.c
  src/course.c
  src/tracks.c
  src/lakitu.c
  src/font.c
  src/cups.c
  src/records.c
  src/menu.c
  src/autopilot.c
  src/env.c
  src/net.c
)
target_include_directories(smkcore PUBLIC include)
target_compile_options(smkcore PRIVATE ${SMK_WARN})
# the RL binding loads a .so, and a shared object cannot link non-PIC
# objects - so the core is built position-independent for everyone
set_target_properties(smkcore PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(smkcore PUBLIC m)

add_executable(smk src/main.c src/audio.c)
pkg_check_modules(SDL2MIXER REQUIRED IMPORTED_TARGET SDL2_mixer)
target_link_libraries(smk PRIVATE smkcore PkgConfig::SDL2 PkgConfig::SDL2MIXER)
target_compile_options(smk PRIVATE ${SMK_WARN})

# Headless check that the asset pipeline agrees with the Python toolkit.
add_executable(smk_selftest tools/selftest.c)
target_link_libraries(smk_selftest PRIVATE smkcore)
target_compile_options(smk_selftest PRIVATE ${SMK_WARN})

add_executable(smk_ailap tools/ailap.c)
target_link_libraries(smk_ailap PRIVATE smkcore)
target_compile_options(smk_ailap PRIVATE ${SMK_WARN})

add_executable(smk_rowcheck tools/rowcheck.c)
target_link_libraries(smk_rowcheck PRIVATE smkcore)
target_compile_options(smk_rowcheck PRIVATE ${SMK_WARN})

add_executable(smk_objhit tools/objhit.c)
target_link_libraries(smk_objhit PRIVATE smkcore)
target_compile_options(smk_objhit PRIVATE ${SMK_WARN})

add_executable(smk_laptest tools/laptest.c)
target_link_libraries(smk_laptest PRIVATE smkcore)
target_compile_options(smk_laptest PRIVATE ${SMK_WARN})

add_executable(smk_accelgrip tools/accelgrip.c)
target_link_libraries(smk_accelgrip PRIVATE smkcore m)
target_compile_options(smk_accelgrip PRIVATE ${SMK_WARN})

add_executable(smk_demoreplay tools/demoreplay.c)
target_link_libraries(smk_demoreplay PRIVATE smkcore m)
target_compile_options(smk_demoreplay PRIVATE ${SMK_WARN})

# The RL environment as a shared object, for the ctypes binding in
# tools/rl/smkenv.py.
add_library(smkenv SHARED src/env.c)
target_link_libraries(smkenv PRIVATE smkcore m)
# on Windows nothing is exported from a DLL unless it is asked for, and
# the ctypes binding looks up every entry point by name
set_target_properties(smkenv PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_options(smkenv PRIVATE ${SMK_WARN})

add_executable(smk_envtest tools/envtest.c)
target_link_libraries(smk_envtest PRIVATE smkcore m)
target_compile_options(smk_envtest PRIVATE ${SMK_WARN})

enable_testing()
add_test(NAME selftest COMMAND smk_selftest ${CMAKE_SOURCE_DIR}/rom/smk_usa.sfc)

# stamp the build with the git short hash so "is it in my build" is visible
execute_process(COMMAND git rev-parse --short HEAD
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                OUTPUT_VARIABLE SMK_GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET)
if(SMK_GIT_HASH)
  target_compile_definitions(smk PRIVATE SMK_BUILD="${SMK_GIT_HASH}")
endif()
