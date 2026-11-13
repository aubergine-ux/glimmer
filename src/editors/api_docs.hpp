#pragma once

#include <span>
#include <string_view>

namespace glim::editors {

struct ApiDoc {
  std::string_view name;
  std::string_view group;
  std::string_view signature;
  std::string_view help;
};

// Every function a cart can call, for autocomplete, signature hints and the
// reference panel. A test checks this list against what the VM registers.
std::span<const ApiDoc> api_docs();
const ApiDoc* find_api(std::string_view name);

}  // namespace glim::editors
