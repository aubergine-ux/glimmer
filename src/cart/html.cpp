#include "cart/html.hpp"

#include <string_view>

#include "cart/bundle.hpp"
#include "cart/image.hpp"
#include "player_template.hpp"

namespace glim::cart {

namespace {

std::string g_runtime_path;

void replace_all(std::string& s, std::string_view from, const std::string& to) {
  for (size_t p = s.find(from); p != std::string::npos; p = s.find(from, p + to.size())) s.replace(p, from.size(), to);
}

std::string escape_html(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '&') out += "&amp;";
    else if (c == '"') out += "&quot;";
    else out += c;
  }
  return out;
}

}  // namespace

void set_web_runtime(const std::string& path) { g_runtime_path = path; }

LoadResult to_html(const Cart& cart, std::string& out) {
  out = kPlayerTemplate;
  std::string runtime;
  std::vector<uint8_t> js;
  if (!g_runtime_path.empty() && read_file(g_runtime_path, js)) {
    std::string code(js.begin(), js.end());
    // An inline script ends at the first "</script", wherever it appears.
    replace_all(code, "</script", "<\\/script");
    runtime = "<script>\n" + code + "\n</script>";
  } else {
    runtime = std::string("<script src=\"") + kHostedRuntime + "\"></script>";
  }
  replace_all(out, "{{TITLE}}", escape_html(cart.get_meta("title", "glimmer game")));
  replace_all(out, "{{CART}}", base64url_encode(pack(cart)));
  replace_all(out, "{{RUNTIME}}", runtime);
  return {true, {}};
}

}  // namespace glim::cart
