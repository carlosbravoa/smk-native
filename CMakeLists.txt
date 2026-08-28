cmake_minimum_required(VERSION 3.16)
project(smk C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

add_library(smkcore STATIC
  src/rom.c
  src/lzc.c
  src/assets.c
  src/mode7.c
  src/kart.c
  src/player.c
  src/demolog.c
  src/effects.c
  src/pickup.c
  src/horizon.c
  src/blocks.c
  src/physics.c
  src/ai.c
  src/sprite.c
  src/course.c
  src/lakitu.c
  src/font.c
  src/cups.c
  src/records.c
  src/menu.c
  src/autopilot.c
)
target_include_directories(smkcore PUBLIC include)
target_compile_options(smkcore PRIVATE -Wall -Wextra -O2)
target_link_libraries(smkcore PUBLIC m)

add_executable(smk src/main.c)
target_link_libraries(smk PRIVATE smkcore PkgConfig::SDL2)
target_compile_options(smk PRIVATE -Wall -Wextra -O2)

# Headless check that the asset pipeline agrees with the Python toolkit.
add_executable(smk_selftest tools/selftest.c)
target_link_libraries(smk_selftest PRIVATE smkcore)
target_compile_options(smk_selftest PRIVATE -Wall -Wextra -O2)

add_executable(smk_ailap tools/ailap.c)
target_link_libraries(smk_ailap PRIVATE smkcore)
target_compile_options(smk_ailap PRIVATE -Wall -Wextra -O2)

add_executable(smk_objhit tools/objhit.c)
target_link_libraries(smk_objhit PRIVATE smkcore)
target_compile_options(smk_objhit PRIVATE -Wall -Wextra -O2)

add_executable(smk_laptest tools/laptest.c)
target_link_libraries(smk_laptest PRIVATE smkcore)
target_compile_options(smk_laptest PRIVATE -Wall -Wextra -O2)

add_executable(smk_demoreplay tools/demoreplay.c)
target_link_libraries(smk_demoreplay PRIVATE smkcore m)
target_compile_options(smk_demoreplay PRIVATE -Wall -Wextra -O2)

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
