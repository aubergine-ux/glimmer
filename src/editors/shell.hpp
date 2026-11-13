#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "editors/editor.hpp"

namespace glim::editors {

// The console's command line: loading, saving and exporting carts, folder
// browsing, and the cart browser. It also plays the boot animation.
class Shell : public Editor {
 public:
  explicit Shell(Studio& studio);

  ui::Icon icon() const override { return ui::Icon::kTerminal; }
  const char* name() const override { return "shell"; }
  void update(ui::Ui& ui, const ui::Rect& body) override;
  void reset() override {}
  std::string status() const override;
  bool wants_escape() const override { return browsing_ || keys_step_ >= 0; }

  void boot();
  bool booting() const { return boot_frame_ >= 0; }
  // Draws the boot animation over the whole screen; returns false once it ends.
  bool update_boot(ui::Ui& ui);

  void set_dir(const std::filesystem::path& dir);
  const std::filesystem::path& dir() const { return dir_; }
  void execute(const std::string& line);
  void print(const std::string& text, int color = -1);
  const std::vector<std::string>& output() const { return text_; }
  void browse();

 private:
  struct Entry {
    std::filesystem::path path;
    std::string title, author;
    std::vector<uint8_t> label;
    bool is_dir = false;
    bool loaded = false;
  };

  std::filesystem::path resolve(const std::string& name, bool must_exist) const;
  std::vector<std::filesystem::path> list(bool carts_only) const;
  void complete();
  void cmd_ls();
  void cmd_cd(const std::string& arg);
  void cmd_load(const std::string& arg);
  void cmd_save(const std::string& arg);
  void cmd_export(const std::string& arg);
  void cmd_info();
  void cmd_help();
  void update_browser(ui::Ui& ui, const ui::Rect& body);
  void update_keys(ui::Ui& ui);
  void print_banner();
  void load_entry(Entry& e);
  void open_entry(Entry& e, bool play);

  std::filesystem::path dir_;
  std::vector<std::string> text_;
  std::vector<int> colors_;
  std::string input_;
  std::vector<std::string> history_lines_;
  int history_pos_ = 0;
  int scroll_ = 0;

  int boot_frame_ = -1;

  int keys_step_ = -1;  // button being rebound by the keys command
  bool keys_prompted_ = false;
  bool browsing_ = false;
  std::vector<Entry> entries_;
  int sel_ = 0;
};

}  // namespace glim::editors
