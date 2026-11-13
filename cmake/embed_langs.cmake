# Embeds lang/*.txt into a header as byte arrays: one entry per language,
# keyed by the file name (lang/sq.txt -> "sq").
function(glimmer_embed_langs out_header)
  file(GLOB lang_files CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/lang/*.txt)
  # Re-run when a translation changes, not just when files come and go.
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${lang_files})
  set(body "")
  set(entries "")
  set(i 0)
  foreach(f ${lang_files})
    get_filename_component(code ${f} NAME_WE)
    file(READ ${f} hex HEX)
    string(LENGTH "${hex}" hex_len)
    math(EXPR size "${hex_len} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
    string(APPEND body "inline constexpr unsigned char kLang${i}[] = {${bytes}0};\n")
    string(APPEND entries "    {\"${code}\", kLang${i}, ${size}},\n")
    math(EXPR i "${i} + 1")
  endforeach()
  file(WRITE ${out_header}.tmp
    "#pragma once\n// Generated from lang/*.txt by CMake.\n#include <cstddef>\nnamespace glim::ui {\n"
    "struct LangFile { const char* code; const unsigned char* data; std::size_t size; };\n"
    "${body}"
    "inline constexpr LangFile kLangFiles[] = {\n${entries}    {\"\", nullptr, 0},\n};\n}  // namespace glim::ui\n")
  configure_file(${out_header}.tmp ${out_header} COPYONLY)
endfunction()
