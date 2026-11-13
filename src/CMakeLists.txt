function(glimmer_module name)
  cmake_parse_arguments(ARG "" "" "SOURCES;DEPS" ${ARGN})
  add_library(glimmer_${name} STATIC ${ARG_SOURCES})
  target_include_directories(glimmer_${name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_options(glimmer_${name} PRIVATE ${GLIMMER_WARNINGS})
  target_link_libraries(glimmer_${name} PUBLIC ${ARG_DEPS})
endfunction()

glimmer_module(core SOURCES core/memory.cpp)
glimmer_module(gfx SOURCES gfx/gfx.cpp gfx/font.cpp DEPS glimmer_core)
glimmer_module(audio SOURCES audio/audio.cpp DEPS glimmer_core)
glimmer_module(input SOURCES input/input.cpp DEPS glimmer_core)
file(READ ${PROJECT_SOURCE_DIR}/web/player.html PLAYER_HTML)
configure_file(cart/player_template.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/generated/player_template.hpp @ONLY)
glimmer_module(cart SOURCES cart/cart.cpp cart/pack.cpp cart/image.cpp cart/gif.cpp cart/bundle.cpp cart/html.cpp
  DEPS glimmer_gfx glimmer_miniz glimmer_stb)
target_include_directories(glimmer_cart PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
glimmer_module(script SOURCES script/vm.cpp script/api.cpp script/prelude.cpp
  DEPS glimmer_gfx glimmer_audio glimmer_input glimmer_lua)
glimmer_module(runtime SOURCES runtime/console.cpp DEPS glimmer_script glimmer_cart)

glimmer_module(crt SOURCES platform/crt.cpp)
glimmer_module(ui SOURCES ui/ui.cpp ui/icons.cpp DEPS glimmer_gfx glimmer_input)
glimmer_module(editors SOURCES editors/history.cpp editors/clipboard.cpp editors/api_docs.cpp editors/lua_lexer.cpp
  editors/editor.cpp editors/studio.cpp editors/code_editor.cpp editors/sprite_editor.cpp editors/map_editor.cpp editors/sfx_editor.cpp editors/music_editor.cpp editors/shell.cpp DEPS glimmer_ui glimmer_runtime)

if(GLIMMER_BUILD_APP)
  if(EMSCRIPTEN OR GLIMMER_FETCH_SDL)
    include(FetchContent)
    FetchContent_Declare(SDL3
      URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
      DOWNLOAD_EXTRACT_TIMESTAMP ON)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL3)
  else()
    find_package(SDL3 CONFIG REQUIRED)
  endif()

  add_executable(glimmer WIN32 MACOSX_BUNDLE
    app/main.cpp app/tools.cpp
    platform/app.cpp)
  target_include_directories(glimmer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_options(glimmer PRIVATE ${GLIMMER_WARNINGS})
  target_link_libraries(glimmer PRIVATE glimmer_editors glimmer_crt SDL3::SDL3)
  set_target_properties(glimmer PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
  if(EMSCRIPTEN)
    set_target_properties(glimmer PROPERTIES SUFFIX ".html")
    target_link_options(glimmer PRIVATE -sSTACK_SIZE=1048576
      --shell-file ${PROJECT_SOURCE_DIR}/web/shell.html
      --preload-file ${PROJECT_SOURCE_DIR}/carts@/carts
      -sEXPORTED_FUNCTIONS=_main,_glim_touch
      -sEXPORTED_RUNTIME_METHODS=ccall,FS,addRunDependency,removeRunDependency)
    set_property(TARGET glimmer APPEND PROPERTY LINK_DEPENDS ${PROJECT_SOURCE_DIR}/web/shell.html)

    # The same program as a single .js with the wasm inlined, for exported
    # HTML games and embeds.
    add_executable(glimmer_player app/main.cpp app/tools.cpp platform/app.cpp)
    target_include_directories(glimmer_player PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(glimmer_player PRIVATE glimmer_editors glimmer_crt SDL3::SDL3)
    set_target_properties(glimmer_player PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR} SUFFIX ".js")
    target_link_options(glimmer_player PRIVATE -sSTACK_SIZE=1048576 -sSINGLE_FILE=1
      -sEXPORTED_FUNCTIONS=_main,_glim_touch
      -sEXPORTED_RUNTIME_METHODS=ccall,FS,addRunDependency,removeRunDependency)
  endif()
endif()
