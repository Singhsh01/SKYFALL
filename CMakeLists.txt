cmake_minimum_required(VERSION 3.20)
project(SKYFALL VERSION 0.5.0 LANGUAGES C CXX)

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
  src/mission/Campaign.cpp
  src/telemetry/FrameStats.cpp
  src/telemetry/Replay.cpp
  src/app/Settings.cpp
)
target_include_directories(skyfall_core PUBLIC src)
find_package(Threads REQUIRED)
target_link_libraries(skyfall_core PUBLIC Threads::Threads)
if(MSVC)
  target_compile_options(skyfall_core PRIVATE /W4)
else()
  target_compile_options(skyfall_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# ---------------------------------------------------------------------------
# Game executable (raylib)
# ---------------------------------------------------------------------------
if(SKYFALL_BUILD_GAME)
  # Prefer an installed raylib (e.g. Homebrew), otherwise download and build raylib 5.5.
  if(EMSCRIPTEN)
    # Browser build: raylib for WebGL 2 (OpenGL ES 3.0), always built from source.
    set(PLATFORM Web CACHE STRING "" FORCE)
    set(OPENGL_VERSION "ES 3.0" CACHE STRING "" FORCE)
  else()
    find_path(SKYFALL_RAYLIB_INCLUDE raylib.h HINTS /opt/homebrew/include /usr/local/include)
    find_library(SKYFALL_RAYLIB_LIB raylib HINTS /opt/homebrew/lib /usr/local/lib)
  endif()
  if(SKYFALL_RAYLIB_INCLUDE AND SKYFALL_RAYLIB_LIB)
    message(STATUS "SKYFALL: using installed raylib ${SKYFALL_RAYLIB_LIB}")
    add_library(skyfall_raylib INTERFACE)
    target_include_directories(skyfall_raylib INTERFACE ${SKYFALL_RAYLIB_INCLUDE})
    target_link_libraries(skyfall_raylib INTERFACE ${SKYFALL_RAYLIB_LIB})
  elseif(SKYFALL_FETCH_RAYLIB)
    message(STATUS "SKYFALL: raylib not found - downloading raylib 5.5")
    include(FetchContent)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(raylib GIT_REPOSITORY https://github.com/raysan5/raylib.git GIT_TAG 5.5 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(raylib)
    if(EMSCRIPTEN)
      target_compile_options(raylib PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/web/glfw_compat.h)
    endif()
    add_library(skyfall_raylib INTERFACE)
    target_link_libraries(skyfall_raylib INTERFACE raylib)
  else()
    message(FATAL_ERROR "raylib not found: brew install raylib, or enable SKYFALL_FETCH_RAYLIB")
  endif()

  add_executable(skyfall
    src/main.cpp
    src/app/Game.cpp
    src/app/Cinematics.cpp
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
  target_link_libraries(skyfall PRIVATE skyfall_core skyfall_raylib)
  if(EMSCRIPTEN)
    # Emit skyfall.js/.wasm/.data and write index.html from web/shell.html ourselves
    # (older Emscripten HTML minifiers are fragile).
    set_target_properties(skyfall PROPERTIES SUFFIX ".js")
    file(READ ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html SKYFALL_SHELL)
    string(REPLACE "{{{ SCRIPT }}}" "<script async src=\"skyfall.js\"></script>" SKYFALL_SHELL "${SKYFALL_SHELL}")
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/index.html "${SKYFALL_SHELL}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html)
    target_link_options(skyfall PRIVATE
      -sUSE_GLFW=3 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sFULL_ES3=1
      -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=268435456 -sTOTAL_STACK=8388608
      -sINVOKE_RUN=0 -sEXPORTED_RUNTIME_METHODS=callMain -sEXIT_RUNTIME=0
      --preload-file ${CMAKE_CURRENT_SOURCE_DIR}/web/fonts@/usr/share/fonts/truetype/dejavu)
  elseif(APPLE)
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
