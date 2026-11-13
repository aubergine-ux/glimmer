#include "ui/i18n.hpp"

#include <map>

#include "lang_files.hpp"

namespace glim::ui {

namespace {

struct Table {
  std::string name;
  std::map<std::string, std::string, std::less<>> strings;
};

std::map<std::string, Table, std::less<>>& tables() {
  static std::map<std::string, Table, std::less<>> t = [] {
    std::map<std::string, Table, std::less<>> m;
    m["en"].name = "English";
    return m;
  }();
  return t;
}

std::string& current() {
  static std::string code = "en";
  return code;
}

bool g_builtins_loaded = false;

void load_builtins() {
  if (g_builtins_loaded) return;
  g_builtins_loaded = true;
  for (const auto& f : kLangFiles)
    if (f.data) load_strings(f.code, std::string_view(reinterpret_cast<const char*>(f.data), f.size));
}

std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
  return s;
}

}  // namespace

void load_strings(std::string_view code, std::string_view text) {
  Table& t = tables()[std::string(code)];
  while (!text.empty()) {
    auto nl = text.find('\n');
    auto line = trim(text.substr(0, nl));
    text = nl == std::string_view::npos ? std::string_view() : text.substr(nl + 1);
    if (line.empty() || line.front() == '#') continue;
    auto eq = line.find(" = ");
    if (eq == std::string_view::npos) continue;
    auto key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 3));
    if (key == "language") t.name = value;
    else if (!value.empty()) t.strings[std::string(key)] = std::string(value);
  }
}

std::string tr(std::string_view english) {
  load_builtins();
  if (current() == "en") return std::string(english);
  auto& t = tables()[current()];
  auto it = t.strings.find(english);
  return it == t.strings.end() ? std::string(english) : it->second;
}

std::vector<std::pair<std::string, std::string>> languages() {
  load_builtins();
  std::vector<std::pair<std::string, std::string>> out;
  for (const auto& [code, t] : tables()) out.emplace_back(code, t.name.empty() ? code : t.name);
  return out;
}

bool set_language(std::string_view code) {
  load_builtins();
  if (!tables().count(code)) return false;
  current() = std::string(code);
  return true;
}

const std::string& language() { return current(); }

std::vector<std::string> known_strings() {
  load_builtins();
  std::vector<std::string> out;
  for (const auto& [code, t] : tables())
    for (const auto& [k, v] : t.strings) out.push_back(k);
  return out;
}

}  // namespace glim::ui
