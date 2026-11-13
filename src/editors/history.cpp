#include "editors/history.hpp"

#include <algorithm>

namespace glim::editors {

namespace {
std::vector<uint8_t>* bytes(cart::Cart& c, Target t) {
  switch (t) {
    case Target::kGfx: return &c.gfx;
    case Target::kFlags: return &c.flags;
    case Target::kMap: return &c.map;
    case Target::kSfx: return &c.sfx;
    case Target::kWave: return &c.wave;
    case Target::kMusic: return &c.music;
    case Target::kPalette: return &c.palette;
    case Target::kCode: return nullptr;
  }
  return nullptr;
}
}  // namespace

std::string History::read(Target t) const {
  if (t == Target::kCode) return cart_->code;
  auto* b = bytes(*cart_, t);
  return std::string(b->begin(), b->end());
}

void History::splice(Target t, size_t offset, size_t old_len, const std::string& with) {
  if (t == Target::kCode) {
    cart_->code.replace(offset, old_len, with);
    return;
  }
  // Data regions have a fixed size, so old_len always equals with.size().
  auto* b = bytes(*cart_, t);
  std::copy(with.begin(), with.end(), b->begin() + std::ptrdiff_t(offset));
}

void History::begin(Target target, int cursor) {
  if (open_ && target == target_) return;
  if (open_) commit(cursor);
  open_ = true;
  target_ = target;
  snapshot_ = read(target);
  cursor_before_ = cursor;
}

bool History::commit(int cursor) {
  if (!open_) return false;
  open_ = false;
  std::string now = read(target_);
  size_t prefix = 0, max_prefix = std::min(now.size(), snapshot_.size());
  while (prefix < max_prefix && now[prefix] == snapshot_[prefix]) ++prefix;
  if (prefix == now.size() && prefix == snapshot_.size()) {
    snapshot_.clear();
    return false;
  }
  size_t suffix = 0, max_suffix = std::min(now.size(), snapshot_.size()) - prefix;
  while (suffix < max_suffix && now[now.size() - 1 - suffix] == snapshot_[snapshot_.size() - 1 - suffix]) ++suffix;

  Step s{target_,
         prefix,
         snapshot_.substr(prefix, snapshot_.size() - prefix - suffix),
         now.substr(prefix, now.size() - prefix - suffix),
         cursor_before_,
         cursor};
  undo_.push_back(std::move(s));
  if (undo_.size() > kMaxSteps) undo_.erase(undo_.begin());
  redo_.clear();
  snapshot_.clear();
  return true;
}

void History::cancel() {
  if (!open_) return;
  open_ = false;
  std::string now = read(target_);
  if (now != snapshot_) splice(target_, 0, now.size(), snapshot_);
  snapshot_.clear();
}

int History::undo() {
  if (open_) commit(cursor_before_);
  if (undo_.empty()) return -1;
  Step s = std::move(undo_.back());
  undo_.pop_back();
  splice(s.target, s.offset, s.after.size(), s.before);
  int cursor = s.cursor_before;
  redo_.push_back(std::move(s));
  return cursor;
}

int History::redo() {
  if (redo_.empty()) return -1;
  Step s = std::move(redo_.back());
  redo_.pop_back();
  splice(s.target, s.offset, s.before.size(), s.after);
  int cursor = s.cursor_after;
  undo_.push_back(std::move(s));
  return cursor;
}

void History::clear() {
  undo_.clear();
  redo_.clear();
  open_ = false;
  snapshot_.clear();
}

}  // namespace glim::editors
