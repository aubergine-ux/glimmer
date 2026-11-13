#include "editors/shell.hpp"

#include <algorithm>
#include <cstdio>

#include "cart/bundle.hpp"
#include "cart/cart.hpp"
#include "editors/studio.hpp"
#include "gfx/font.hpp"

namespace glim::editors {

namespace {

using input::key_from_name;
namespace fs = std::filesystem;

constexpr int kText = 5, kDim = 6, kPrompt = 30, kError = 11, kGood = 21, kTitle = 17, kDirColor = 25;
constexpr int kMaxLines = 400;
constexpr int kBootFrames = 170;
constexpr int kCardW = 80, kCardH = 45, kCardsX = 3, kCardsY = 2;
constexpr const char* kCartExts[] = {".glim", ".png", ".lua"};
constexpr const char* kPlayUrl = "https://armd.one/glimmer/play/#c=";
constexpr size_t kMaxShareUrl = 8000;  // what browsers and chat apps reliably accept

bool is_cart(const fs::path& p) {
  std::string e = p.extension().string();
  for (const char* x : kCartExts)
    if (e == x) return true;
  return false;
}

std::vector<std::string> split_words(const std::string& line) {
  std::vector<std::string> out;
  std::string cur;
  bool quoted = false;
  for (char c : line) {
    if (c == '"') {
      quoted = !quoted;
    } else if (c == ' ' && !quoted) {
      if (!cur.empty()) out.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

// Big letters for the boot logo, drawn from the console font.
void big_text(gfx::Gfx& g, std::string_view s, int x, int y, int scale, int color) {
  for (char ch : s) {
    for (int row = 0; row < gfx::kGlyphH; ++row) {
      uint8_t bits = gfx::glyph_row(uint8_t(ch), row);
      for (int col = 0; col < gfx::kGlyphW; ++col)
        if ((bits >> (gfx::kGlyphW - 1 - col)) & 1)
          g.rectfill(x + col * scale, y + row * scale, x + (col + 1) * scale - 1, y + (row + 1) * scale - 1, color);
    }
    x += gfx::kCharW * scale;
  }
}

constexpr const char* kBootLines[] = {
    "glimmer 0.1.0  spec 1",
    "256x144  32 colors  4 voices  lua 5.4",
    "type help to see commands, or browse for carts",
};

}  // namespace

Shell::Shell(Studio& studio) : Editor(studio), dir_(fs::current_path()) {
  for (const char* l : kBootLines) print(l, l == kBootLines[2] ? kDim : kText);
}

void Shell::set_dir(const fs::path& dir) {
  std::error_code ec;
  fs::create_directories(dir, ec);
  dir_ = fs::weakly_canonical(dir, ec);
  if (ec) dir_ = dir;
}

void Shell::print(const std::string& text, int color) {
  // Wrap long lines at the screen width.
  constexpr size_t kCols = (kScreenW - 4) / gfx::kCharW;
  size_t start = 0;
  do {
    text_.push_back(text.substr(start, kCols));
    colors_.push_back(color < 0 ? kText : color);
    start += kCols;
  } while (start < text.size());
  if (int(text_.size()) > kMaxLines) {
    text_.erase(text_.begin(), text_.begin() + (int(text_.size()) - kMaxLines));
    colors_.erase(colors_.begin(), colors_.begin() + (int(colors_.size()) - kMaxLines));
  }
  scroll_ = 0;
}

fs::path Shell::resolve(const std::string& name, bool must_exist) const {
  fs::path p = fs::path(name).is_absolute() ? fs::path(name) : dir_ / name;
  std::error_code ec;
  if (fs::exists(p, ec) || !must_exist) {
    if (!must_exist && !p.has_extension()) p += ".glim";
    return p;
  }
  for (const char* e : kCartExts) {
    fs::path q = p;
    q += e;
    if (fs::exists(q, ec)) return q;
  }
  return {};
}

std::vector<fs::path> Shell::list(bool carts_only) const {
  std::vector<fs::path> dirs, carts;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(dir_, ec)) {
    if (e.path().filename().string().starts_with(".")) continue;
    if (e.is_directory(ec)) dirs.push_back(e.path());
    else if (is_cart(e.path())) carts.push_back(e.path());
  }
  std::sort(dirs.begin(), dirs.end());
  std::sort(carts.begin(), carts.end());
  if (carts_only) return carts;
  dirs.insert(dirs.end(), carts.begin(), carts.end());
  return dirs;
}

void Shell::cmd_help() {
  print("commands", kTitle);
  static constexpr const char* kHelp[][2] = {
      {"ls", "list carts and folders"},
      {"cd <folder>", "change folder (cd .. goes up)"},
      {"load <cart>", "open a cart in the editors"},
      {"run", "play the current cart"},
      {"save [name]", "save the cart (.glim, or .png)"},
      {"export <file>", "write a .png, .html or .glim copy"},
      {"new", "start a fresh cart"},
      {"browse", "pick a cart by its cover"},
      {"info", "sizes and limits of this cart"},
      {"share", "copy a link that plays this cart"},
      {"bundle <name>", "build a standalone game"},
      {"title <text>", "name the cart (also author)"},
      {"edit", "jump to the code editor"},
      {"cls", "clear the screen"},
      {"reboot", "replay the boot sequence"},
  };
  for (const auto& h : kHelp) {
    std::string line = std::string("  ") + h[0];
    line.resize(17, ' ');
    print(line + h[1], kText);
  }
  print("esc in a game opens the editors; f6 screenshot, f8 gif", kDim);
}

void Shell::cmd_ls() {
  auto entries = list(false);
  print(dir_.string(), kDim);
  if (entries.empty()) print("  (no carts here)", kDim);
  for (const auto& p : entries) {
    std::error_code ec;
    if (fs::is_directory(p, ec)) print("  " + p.filename().string() + "/", kDirColor);
    else print("  " + p.filename().string(), kText);
  }
}

void Shell::cmd_cd(const std::string& arg) {
  if (arg.empty()) {
    print(dir_.string(), kDim);
    return;
  }
  fs::path p = arg == ".." ? dir_.parent_path() : (fs::path(arg).is_absolute() ? fs::path(arg) : dir_ / arg);
  std::error_code ec;
  if (!fs::is_directory(p, ec)) {
    print("no folder named " + arg, kError);
    return;
  }
  dir_ = fs::weakly_canonical(p, ec);
  print(dir_.string(), kDim);
}

void Shell::cmd_load(const std::string& arg) {
  if (arg.empty()) {
    print("load what? try ls", kError);
    return;
  }
  fs::path p = resolve(arg, true);
  if (p.empty()) {
    print("can't find " + arg, kError);
    return;
  }
  cart::Cart c;
  auto r = cart::load_file(p.string(), c);
  if (!r.ok) {
    print(r.error, kError);
    return;
  }
  // .lua files load as code; saving writes a .glim beside them.
  fs::path save_path = p.extension() == ".lua" ? fs::path(p).replace_extension(".glim") : p;
  studio_.open(c, save_path.string(), false);
  print("loaded " + p.filename().string() + (c.get_meta("title").empty() ? "" : "  \"" + c.get_meta("title") + "\""), kGood);
}

void Shell::cmd_save(const std::string& arg) {
  if (!arg.empty()) {
    if (studio_.save_as(resolve(arg, false).string())) print("saved " + resolve(arg, false).filename().string(), kGood);
    else print("can't save " + arg, kError);
    return;
  }
  if (studio_.path().empty()) {
    print("save as what? save <name>", kError);
    return;
  }
  if (studio_.save()) print("saved " + fs::path(studio_.path()).filename().string(), kGood);
}

void Shell::cmd_export(const std::string& arg) {
  if (arg.empty()) {
    print("export to what? export mygame.png", kError);
    return;
  }
  fs::path p = resolve(arg, false);
  cart::Cart c = studio_.cart();
  if (c.label.empty() && p.extension() == ".png") capture_label(c);
  auto r = cart::save_file(p.string(), c);
  if (r.ok) print("exported " + p.filename().string(), kGood);
  else print(r.error, kError);
}

void Shell::cmd_info() {
  const auto& c = studio_.cart();
  print(c.get_meta("title", "untitled") + (c.get_meta("author").empty() ? "" : " by " + c.get_meta("author")), kTitle);
  size_t lines = size_t(std::count(c.code.begin(), c.code.end(), '\n')) + 1;
  print("code    " + std::to_string(c.code.size()) + " / " + std::to_string(cart::kMaxCodeBytes) + " bytes, " +
            std::to_string(lines) + " lines",
        kText);
  size_t packed = cart::pack(c).size();
  int pct = int(packed * 100 / cart::kPngCapacity);
  print("packed  " + std::to_string(packed) + " bytes (" + std::to_string(pct) + "% of a png cart)",
        pct > 100 ? kError : kText);
  int sprites = 0;
  for (int n = 0; n < kSprites; ++n) {
    int sx = (n % 16) * 8, sy = (n / 16) * 8;
    bool used = false;
    for (int y = 0; y < 8 && !used; ++y)
      for (int x = 0; x < 8 && !used; ++x) used = c.gfx[size_t(sy + y) * kSheetW + size_t(sx + x)] != 0;
    sprites += used;
  }
  print("sprites " + std::to_string(sprites) + " / " + std::to_string(kSprites), kText);
}

void Shell::execute(const std::string& line) {
  print("> " + line, kPrompt);
  auto words = split_words(line);
  if (words.empty()) return;
  if (history_lines_.empty() || history_lines_.back() != line) history_lines_.push_back(line);
  history_pos_ = int(history_lines_.size());
  std::string cmd = words[0];
  std::string arg = line.size() > cmd.size() ? line.substr(line.find(cmd) + cmd.size() + 1) : "";
  while (!arg.empty() && arg.front() == ' ') arg.erase(0, 1);
  if (arg.size() >= 2 && arg.front() == '"' && arg.back() == '"') arg = arg.substr(1, arg.size() - 2);

  if (cmd == "help" || cmd == "?") cmd_help();
  else if (cmd == "ls" || cmd == "dir") cmd_ls();
  else if (cmd == "cd") cmd_cd(arg);
  else if (cmd == "load" || cmd == "open") cmd_load(arg);
  else if (cmd == "save") cmd_save(arg);
  else if (cmd == "export") cmd_export(arg);
  else if (cmd == "info") cmd_info();
  else if (cmd == "share") {
    std::string url = kPlayUrl + cart::base64url_encode(cart::pack(studio_.cart()));
    if (url.size() > kMaxShareUrl) {
      print("this cart is too big for a link (" + std::to_string(url.size()) + " chars); export a png", kError);
    } else {
      studio_.set_clipboard(url);
      print("link copied (" + std::to_string(url.size()) + " chars):", kGood);
      print(url, kDim);
    }
  } else if (cmd == "bundle") {
    if (studio_.runtime().empty()) {
      print("bundling isn't available here", kError);
    } else if (arg.empty()) {
      print("bundle what? bundle mygame", kError);
    } else {
      fs::path out = dir_ / arg;
      bool app = fs::path(studio_.runtime()).extension() == ".app";
      if (app && out.extension() != ".app") out += ".app";
      if (!app && fs::path(studio_.runtime()).extension() == ".exe" && out.extension() != ".exe") out += ".exe";
      auto r = cart::bundle(studio_.runtime(), studio_.cart(), out.string());
      if (r.ok) print("built " + out.filename().string() + ": share it, or upload it to itch.io", kGood);
      else print(r.error, kError);
    }
  }
  else if (cmd == "run") studio_.play();
  else if (cmd == "new") {
    studio_.new_cart();
    print("new cart", kGood);
  } else if (cmd == "edit") studio_.select_editor(1);
  else if (cmd == "browse" || cmd == "splore") browse();
  else if (cmd == "cls") {
    text_.clear();
    colors_.clear();
  } else if (cmd == "reboot") boot();
  else if (cmd == "title" || cmd == "author") {
    if (arg.empty()) {
      print(studio_.cart().get_meta(cmd, "(none)"), kText);
    } else {
      studio_.cart().set_meta(cmd, arg);
      studio_.mark_dirty();
      print(cmd + " set", kGood);
    }
  } else {
    print("unknown command " + cmd + ". try help", kError);
  }
}

void Shell::complete() {
  // Tab completes the last word against files in the current folder.
  auto space = input_.rfind(' ');
  if (space == std::string::npos) return;
  std::string prefix = input_.substr(space + 1);
  std::vector<std::string> matches;
  for (const auto& p : list(false)) {
    std::string name = p.filename().string();
    if (name.starts_with(prefix)) matches.push_back(name);
  }
  if (matches.size() == 1) {
    input_ = input_.substr(0, space + 1) + matches[0];
  } else if (matches.size() > 1) {
    std::string common = matches[0];
    for (const auto& m : matches)
      while (!m.starts_with(common)) common.pop_back();
    input_ = input_.substr(0, space + 1) + common;
    std::string all;
    for (const auto& m : matches) all += m + "  ";
    print(all, kDim);
  }
}

void Shell::boot() {
  boot_frame_ = 0;
  text_.clear();
  colors_.clear();
}

bool Shell::update_boot(ui::Ui& ui) {
  if (boot_frame_ < 0) return false;
  auto& g = ui.g();
  auto& audio = studio_.console().audio;
  int f = boot_frame_++;
  bool skip = ui.in().any_key_pressed() || ui.clicked(0);
  g.cls(0);

  if (f < 16) {
    // Power on: a bright line opening out from the center, like an old tube.
    int w = kScreenW * (f + 1) / 16;
    g.rectfill(kScreenW / 2 - w / 2, kScreenH / 2, kScreenW / 2 + w / 2, kScreenH / 2, 31);
  } else if (f < 24) {
    int h = kScreenH * (f - 15) / 8;
    g.rectfill(0, kScreenH / 2 - h / 2, kScreenW - 1, kScreenH / 2 + h / 2, 1);
  } else {
    g.cls(1);
    // Twinkling stars, seeded so every boot looks the same.
    uint32_t seed = 0x51ed;
    for (int i = 0; i < 50; ++i) {
      seed = seed * 1103515245u + 12345u;
      int x = int(seed >> 8) % kScreenW, y = int(seed >> 16) % kScreenH;
      if (((f / 6) + i) % 7) g.pset(x, y, i % 3 ? 7 : 5);
    }
    static constexpr int kColors[7] = {29, 30, 17, 21, 28, 25, 4};
    static constexpr int kChime[7] = {24, 28, 31, 35, 36, 40, 43};
    const char* word = "GLIMMER";
    int left = kScreenW / 2 - 7 * 16 / 2;
    for (int i = 0; i < 7; ++i) {
      int start = 24 + i * 7, t = f - start;
      if (t < 0) continue;
      // Drop in with a small bounce.
      float k = std::min(1.f, t / 14.f);
      float bounce = k < 1.f ? (1.f - k) * (1.f - k) * 60.f : 0.f;
      if (t > 14 && t < 20) bounce = -float(std::min(t - 14, 20 - t)) * 1.5f;
      int y = 40 - int(bounce);
      char ch[2] = {word[i], 0};
      big_text(g, ch, left + i * 16 + 1, y + 1, 4, 2);
      big_text(g, ch, left + i * 16, y, 4, kColors[i]);
      if (t == 14) audio.note(i % 3, kChime[i], 6, 5);
      if (t == 22) audio.note(i % 3, -1, 0, 0);
    }
    if (f >= 90) {
      int chars = (f - 90) * 2;
      int y = 82;
      for (const char* line : kBootLines) {
        std::string s(line);
        int n = std::clamp(chars, 0, int(s.size()));
        g.print(s.substr(0, size_t(n)), kScreenW / 2 - gfx::Gfx::text_width(s) / 2, y, line == kBootLines[2] ? kDim : kText);
        chars -= int(s.size());
        y += 8;
      }
    }
  }

  if (skip || f >= kBootFrames) {
    for (int ch = 0; ch < 3; ++ch) audio.note(ch, -1, 0, 0);
    boot_frame_ = -1;
    for (const char* l : kBootLines) print(l, l == kBootLines[2] ? kDim : kText);
    return false;
  }
  return true;
}

void Shell::browse() {
  browsing_ = true;
  entries_.clear();
  std::error_code ec;
  if (dir_.has_parent_path() && dir_.parent_path() != dir_) entries_.push_back({dir_.parent_path(), "..", "", {}, true, true});
  for (const auto& p : list(false)) {
    Entry e;
    e.path = p;
    e.is_dir = fs::is_directory(p, ec);
    e.title = e.is_dir ? p.filename().string() + "/" : p.stem().string();
    entries_.push_back(std::move(e));
  }
  sel_ = std::min(sel_, std::max(0, int(entries_.size()) - 1));
}

void Shell::load_entry(Entry& e) {
  if (e.loaded) return;
  e.loaded = true;
  if (e.is_dir) return;
  cart::Cart c;
  if (!cart::load_file(e.path.string(), c).ok) return;
  if (!c.get_meta("title").empty()) e.title = c.get_meta("title");
  e.author = c.get_meta("author");
  // No saved label: run the cart briefly and use what it shows.
  if (c.label.empty()) capture_label(c, 120);
  e.label = c.label;
}

void Shell::open_entry(Entry& e, bool play) {
  if (e.is_dir) {
    dir_ = fs::weakly_canonical(e.path);
    sel_ = 0;
    browse();
    return;
  }
  browsing_ = false;
  cmd_load(e.path.string());
  if (play) studio_.play();
  else studio_.select_editor(1);
}

void Shell::update_browser(ui::Ui& ui, const ui::Rect& body) {
  const auto& th = ui.theme();
  auto& g = ui.g();
  auto& in = ui.in();
  int per_page = kCardsX * kCardsY;
  int n = int(entries_.size());
  if (in.keyr(key_from_name("left"))) sel_ = std::max(0, sel_ - 1);
  if (in.keyr(key_from_name("right"))) sel_ = std::min(n - 1, sel_ + 1);
  if (in.keyr(key_from_name("up"))) sel_ = std::max(0, sel_ - kCardsX);
  if (in.keyr(key_from_name("down"))) sel_ = std::min(n - 1, sel_ + kCardsX);
  if (in.keyp(key_from_name("escape"))) browsing_ = false;
  if (n > 0 && in.keyp(key_from_name("return"))) {
    open_entry(entries_[size_t(sel_)], true);
    return;
  }
  if (n > 0 && in.keyp(key_from_name("e"))) {
    open_entry(entries_[size_t(sel_)], false);
    return;
  }

  ui.fill(body, th.panel);
  int page = n ? sel_ / per_page : 0, pages = std::max(1, (n + per_page - 1) / per_page);
  std::string head = dir_.filename().string();
  if (head.empty()) head = dir_.string();
  ui.text(head, 4, body.y + 3, th.text);
  std::string pg = std::to_string(page + 1) + "/" + std::to_string(pages);
  ui.text(pg, kScreenW - 4 - gfx::Gfx::text_width(pg), body.y + 3, th.dim);
  if (n == 0) ui.text("no carts in this folder", 4, body.y + 20, th.dim);

  for (int i = 0; i < per_page; ++i) {
    int idx = page * per_page + i;
    if (idx >= n) break;
    Entry& e = entries_[size_t(idx)];
    load_entry(e);
    int x = 4 + (i % kCardsX) * (kCardW + 4), y = body.y + 12 + (i / kCardsX) * (kCardH + 14);
    ui::Rect card{x, y, kCardW, kCardH};
    bool sel = idx == sel_;
    if (e.label.size() == addr::kScreenSize) {
      // Downscaled cover: sample the label at the card's resolution.
      for (int py = 0; py < kCardH; ++py)
        for (int px = 0; px < kCardW; ++px)
          g.pset(x + px, y + py, e.label[size_t(py * kScreenH / kCardH) * kScreenW + size_t(px * kScreenW / kCardW)]);
    } else {
      ui.fill(card, e.is_dir ? th.raised : th.bg);
      if (e.is_dir) {
        ui.icon(ui::Icon::kFolder, x + kCardW / 2 - 4, y + kCardH / 2 - 4, th.dim);
      } else {
        for (int s = 0; s < 12; ++s) g.pset(x + (s * 37 + idx * 11) % kCardW, y + (s * 23 + idx * 7) % kCardH, 7);
        ui.text_centered("?", card, th.raised + 1);
      }
    }
    g.rect(x - 1, y - 1, x + kCardW, y + kCardH, sel ? 31 : th.shadow);
    if (sel) g.rect(x - 2, y - 2, x + kCardW + 1, y + kCardH + 1, th.accent);
    std::string title = e.title.size() > 19 ? e.title.substr(0, 18) + "." : e.title;
    ui.text(title, x, y + kCardH + 3, sel ? th.text : th.dim);
    if (ui.clicked_in(card)) {
      if (sel && ui.double_clicked()) {
        open_entry(e, true);
        return;
      }
      sel_ = idx;
    }
  }
}

void Shell::update(ui::Ui& ui, const ui::Rect& body) {
  if (browsing_) {
    update_browser(ui, body);
    return;
  }
  auto& in = ui.in();
  const auto& th = ui.theme();
  if (!in.ctrl() && !in.alt()) {
    for (char c : in.text())
      if (uint8_t(c) >= 32 && uint8_t(c) < 127) input_ += c;
    if (in.keyr(key_from_name("backspace")) && !input_.empty()) input_.pop_back();
    if (in.keyp(key_from_name("return"))) {
      std::string line = input_;
      input_.clear();
      execute(line);
      if (browsing_ || studio_.playing()) return;
    }
    if (in.keyp(key_from_name("tab"))) complete();
    if (in.keyr(key_from_name("up")) && history_pos_ > 0) input_ = history_lines_[size_t(--history_pos_)];
    if (in.keyr(key_from_name("down"))) {
      if (history_pos_ + 1 < int(history_lines_.size())) input_ = history_lines_[size_t(++history_pos_)];
      else {
        history_pos_ = int(history_lines_.size());
        input_.clear();
      }
    }
    if (in.keyr(key_from_name("pageup"))) scroll_ += 10;
    if (in.keyr(key_from_name("pagedown"))) scroll_ -= 10;
  }
  if (ui.hovered(body) && ui.wheel()) scroll_ += ui.wheel() * 3;

  ui.fill(body, th.bg);
  int rows = body.h / gfx::kCharH - 1;
  scroll_ = std::clamp(scroll_, 0, std::max(0, int(text_.size()) - rows));
  int first = std::max(0, int(text_.size()) - rows - scroll_);
  for (int r = 0; r < rows && first + r < int(text_.size()); ++r)
    ui.text(text_[size_t(first + r)], 2, body.y + 2 + r * gfx::kCharH, colors_[size_t(first + r)]);
  int y = body.y + 2 + std::min(rows, int(text_.size())) * gfx::kCharH;
  ui.text(">", 2, y, kPrompt);
  std::string shown = input_;
  constexpr int kInputCols = (kScreenW - 14) / gfx::kCharW;
  if (int(shown.size()) > kInputCols) shown = shown.substr(shown.size() - size_t(kInputCols));
  ui.text(shown, 8, y, kText);
  if ((ui.frame() / 16) % 2 == 0) {
    int cx = 8 + int(shown.size()) * gfx::kCharW;
    ui.g().rectfill(cx, y, cx + 2, y + 4, kPrompt);
  }
}

std::string Shell::status() const {
  if (browsing_) return "arrows choose  enter plays  e edits  esc closes";
  return dir_.string();
}

}  // namespace glim::editors
