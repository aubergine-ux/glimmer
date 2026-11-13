#pragma once

#include <string>

#include "editors/history.hpp"
#include "ui/ui.hpp"

namespace glim::editors {

class Studio;

// One tab of the studio. Editors are immediate mode: update() handles input
// and draws in a single pass, every frame the editor is visible.
class Editor {
 public:
  explicit Editor(Studio& studio);
  virtual ~Editor() = default;

  virtual ui::Icon icon() const = 0;
  virtual const char* name() const = 0;
  virtual void update(ui::Ui& ui, const ui::Rect& body) = 0;

  // Called when a different cart is loaded into the studio.
  virtual void reset() { history_.clear(); }
  // True while the editor has something Esc should dismiss (a popup, a
  // selection); otherwise Esc goes back to a paused game.
  virtual bool wants_escape() const { return false; }
  // Context-sensitive hint for the status bar.
  virtual std::string status() const { return {}; }

  virtual void undo();
  virtual void redo();
  virtual void copy() {}
  virtual void cut() {}
  virtual void paste() {}

 protected:
  Studio& studio_;
  History history_;
};

}  // namespace glim::editors
