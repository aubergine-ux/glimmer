#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace glim::ui {

// Console UI text is written in English and looked up through tr(). A
// language file (lang/<code>.txt) maps each English string to a translation,
// one "English = translation" per line; '#' starts a comment and {0}, {1}...
// mark values filled in at runtime. Strings without a translation stay English.

std::string tr(std::string_view english);

template <class... Args>
std::string tr(std::string_view english, const Args&... args) {
  std::string s = tr(english);
  std::string values[] = {std::string(args)...};
  for (size_t i = 0; i < sizeof...(Args); ++i) {
    std::string mark = "{" + std::to_string(i) + "}";
    for (size_t p = s.find(mark); p != std::string::npos; p = s.find(mark, p + values[i].size()))
      s.replace(p, mark.size(), values[i]);
  }
  return s;
}

// Built-in languages as (code, name in that language).
std::vector<std::pair<std::string, std::string>> languages();
bool set_language(std::string_view code);
const std::string& language();
// Adds or replaces translations for a language, e.g. from a user's file.
void load_strings(std::string_view code, std::string_view text);
// The English strings a file can translate, for writing new language files.
std::vector<std::string> known_strings();

}  // namespace glim::ui
