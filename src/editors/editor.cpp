#include "editors/editor.hpp"

#include "editors/studio.hpp"

namespace glim::editors {

Editor::Editor(Studio& studio) : studio_(studio), history_(studio.cart_ref()) {}

void Editor::undo() {
  if (history_.undo() < 0) studio_.message("nothing to undo");
}

void Editor::redo() {
  if (history_.redo() < 0) studio_.message("nothing to redo");
}

}  // namespace glim::editors
