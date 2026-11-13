#include <doctest.h>

#include <filesystem>
#include <memory>

#include "cart/image.hpp"
#include "editors/shell.hpp"
#include "editors/studio.hpp"

using namespace glim;
using namespace glim::editors;
namespace fs = std::filesystem;

namespace {
struct Fixture {
  fs::path dir = fs::temp_directory_path() / "glimmer_shell_test";
  std::unique_ptr<Console> con = std::make_unique<Console>();
  Studio studio{*con, {}};
  Fixture() {
    fs::remove_all(dir);
    fs::create_directories(dir / "sub");
    std::string cart = "glimmer cart 1\n[meta]\ntitle = demo\n[code]\nx = 1\n";
    cart::write_file((dir / "demo.glim").string(), {reinterpret_cast<const uint8_t*>(cart.data()), cart.size()});
    studio.new_cart();
    studio.shell().set_dir(dir);
  }
  ~Fixture() { fs::remove_all(dir); }
  Shell& sh() { return studio.shell(); }
  bool said(const std::string& needle) {
    for (const auto& l : sh().output())
      if (l.find(needle) != std::string::npos) return true;
    return false;
  }
};
}  // namespace

TEST_CASE("shell lists, changes folders and loads carts without extensions") {
  Fixture f;
  f.sh().execute("ls");
  CHECK(f.said("demo.glim"));
  CHECK(f.said("sub/"));
  f.sh().execute("load demo");
  CHECK(f.said("loaded demo.glim"));
  CHECK(f.studio.cart().code == "x = 1");
  f.sh().execute("cd sub");
  CHECK(f.sh().dir().filename() == "sub");
  f.sh().execute("cd ..");
  CHECK(f.sh().dir().filename() == "glimmer_shell_test");
  f.sh().execute("load nothing");
  CHECK(f.said("can't find nothing"));
}

TEST_CASE("shell saves, exports and edits metadata") {
  Fixture f;
  f.sh().execute("title My Game");
  CHECK(f.studio.cart().get_meta("title") == "My Game");
  f.sh().execute("save mine");
  CHECK(fs::exists(f.dir / "mine.glim"));
  f.sh().execute("export mine.png");
  CHECK(fs::exists(f.dir / "mine.png"));
  cart::Cart back;
  REQUIRE(cart::load_file((f.dir / "mine.png").string(), back).ok);
  CHECK(back.get_meta("title") == "My Game");
  CHECK(back.label.size() == addr::kScreenSize);  // label captured on export
  f.sh().execute("frobnicate");
  CHECK(f.said("unknown command"));
}
