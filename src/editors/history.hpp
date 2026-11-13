#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cart/cart.hpp"

namespace glim::editors {

// Which part of the cart an edit touched.
enum class Target : uint8_t { kCode, kGfx, kFlags, kMap, kSfx, kWave, kMusic, kPalette };

// Undo/redo for one editor. begin() snapshots the target buffer, the editor
// modifies the cart freely, and commit() stores only the bytes that changed
// (common prefix and suffix trimmed), so even large buffers are cheap.
class History {
 public:
  explicit History(cart::Cart*& cart) : cart_(cart) {}

  // Opening an already-open step keeps the original snapshot, which lets
  // callers coalesce bursts such as typing into one undo step.
  void begin(Target target, int cursor = 0);
  bool open() const { return open_; }
  // Returns false (and records nothing) when the buffer didn't change.
  bool commit(int cursor = 0);
  void cancel();

  // Each returns the cursor recorded with the step, or -1 when there was nothing to do.
  int undo();
  int redo();
  bool can_undo() const { return !undo_.empty(); }
  bool can_redo() const { return !redo_.empty(); }
  void clear();

 private:
  struct Step {
    Target target;
    size_t offset;
    std::string before, after;
    int cursor_before, cursor_after;
  };

  std::string read(Target t) const;
  void splice(Target t, size_t offset, size_t old_len, const std::string& with);

  static constexpr size_t kMaxSteps = 200;

  cart::Cart*& cart_;
  std::vector<Step> undo_, redo_;
  bool open_ = false;
  Target target_ = Target::kCode;
  std::string snapshot_;
  int cursor_before_ = 0;
};

}  // namespace glim::editors
