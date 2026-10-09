cmake_minimum_required(VERSION 3.20)
project(SKYFALL VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(SKYFALL_BUILD_GAME  "Build the raylib executable" ON)
option(SKYFALL_BUILD_TESTS "Build unit tests" ON)
option(SKYFALL_FETCH_RAYLIB "Download raylib 5.5 if it is not installed" ON)

# ---------------------------------------------------------------------------
# Core simulation library: NO raylib dependency (physics, combat, missions).
# ---------------------------------------------------------------------------
add_library(skyfall_core STATIC
  src/sim/Terrain.cpp
  src/sim/FlightModel.cpp
  src/aircraft/AircraftProfile.cpp
  src/combat/Combat.cpp
  src/mission/Mission.cpp
  src/telemetry/FrameStats.cpp
  src/app/Settings.cpp
)
target_include_directories(skyfall_core PUBLIC src)
if(MSVC)
  target_compile_options(skyfall_core PRIVATE /W4)
else()
  target_compile_options(skyfall_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# ---------------------------------------------------------------------------
# Game executable (raylib)
# ---------------------------------------------------------------------------
if(SKYFALL_BUILD_GAME)
  find_package(raylib 5.0 QUIET)
  if(NOT raylib_FOUND AND SKYFALL_FETCH_RAYLIB)
    include(FetchContent)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(raylib
      URL https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz)
    FetchContent_MakeAvailable(raylib)
  endif()

  add_executable(skyfall
    src/main.cpp
    src/app/Game.cpp
    src/app/Input.cpp
    src/render/Renderer.cpp
    src/render/JetMesh.cpp
    src/render/Effects.cpp
    src/render/WorldMeshes.cpp
    src/ui/Hud.cpp
    src/ui/Cockpit.cpp
    src/ui/Menus.cpp
    src/ui/Font.cpp
    src/audio/Audio.cpp
  )
  target_link_libraries(skyfall PRIVATE skyfall_core raylib)
  if(APPLE)
    target_link_libraries(skyfall PRIVATE "-framework IOKit" "-framework Cocoa" "-framework OpenGL" "-framework CoreAudio" "-framework CoreVideo")
  elseif(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(skyfall PRIVATE Threads::Threads m dl GL X11)
  endif()
  if(NOT MSVC)
    target_compile_options(skyfall PRIVATE -Wall -Wno-unused-parameter -Wno-missing-field-initializers)
  endif()
endif()

# ---------------------------------------------------------------------------
# Tests (pure C++, run headless in CI)
# ---------------------------------------------------------------------------
if(SKYFALL_BUILD_TESTS)
  enable_testing()
  add_executable(skyfall_tests tests/sim_tests.cpp)
  target_link_libraries(skyfall_tests PRIVATE skyfall_core)
  add_test(NAME skyfall_tests COMMAND skyfall_tests)
endif()
