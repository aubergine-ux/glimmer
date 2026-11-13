file(GLOB LUA_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/*.c)
add_library(glimmer_lua STATIC ${LUA_SOURCES})
target_include_directories(glimmer_lua SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lua/src)
# Built as C++ so lua_error unwinds with exceptions instead of longjmp, which
# lets the bindings hold RAII objects across calls that may raise.
set_source_files_properties(${LUA_SOURCES} PROPERTIES LANGUAGE CXX)
target_compile_options(glimmer_lua PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-w>)
if(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
  target_compile_definitions(glimmer_lua PRIVATE LUA_USE_LINUX)
elseif(APPLE)
  target_compile_definitions(glimmer_lua PRIVATE LUA_USE_MACOSX)
endif()

add_library(glimmer_miniz STATIC miniz/miniz.c)
target_include_directories(glimmer_miniz SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/miniz)
target_compile_definitions(glimmer_miniz PUBLIC MINIZ_NO_ZLIB_COMPATIBLE_NAMES MINIZ_NO_ARCHIVE_APIS MINIZ_NO_STDIO)

add_library(glimmer_stb INTERFACE)
target_include_directories(glimmer_stb SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/stb)

add_library(glimmer_doctest INTERFACE)
target_include_directories(glimmer_doctest SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/doctest)
