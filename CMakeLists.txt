cmake_minimum_required(VERSION 3.24)

project(glimmer VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(GLIMMER_BUILD_TESTS "Build unit tests" ON)
option(GLIMMER_BUILD_APP "Build the SDL3 desktop/web frontend" ON)
option(GLIMMER_FETCH_SDL "Download and build SDL3 instead of using the system package" OFF)

if(MSVC)
  set(GLIMMER_WARNINGS /W4 /permissive- /wd4244 /wd4267)
else()
  set(GLIMMER_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wno-sign-compare)
endif()

if(EMSCRIPTEN)
  # Lua raises errors as C++ exceptions, so the web build needs real EH.
  add_compile_options(-fwasm-exceptions)
  add_link_options(-fwasm-exceptions -sALLOW_MEMORY_GROWTH=1)
endif()

add_compile_definitions(GLIMMER_VERSION="${PROJECT_VERSION}")

add_subdirectory(third_party)
add_subdirectory(src)

if(GLIMMER_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
