#include <doctest.h>

#include <random>

#include "cart/cart.hpp"
#include "cart/image.hpp"

using namespace glim;
using namespace glim::cart;

namespace {

Cart random_cart(uint32_t seed) {
  std::mt19937 rng(seed);
  Cart c;
  c.set_meta("title", "Round Trip");
  c.set_meta("author", "tests");
  c.code = "function _draw()\n  cls(1)\n[[not a header]]\n\\backslash\n  print(\"hi\")\nend\n";
  for (auto& b : c.gfx) b = uint8_t(rng() % 32);
  for (auto& b : c.flags) b = uint8_t(rng());
  for (size_t i = 0; i < c.map.size(); i += 97) c.map[i] = uint8_t(rng());
  for (auto& b : c.sfx) b = uint8_t(rng());
  for (auto& b : c.wave) b = uint8_t(rng());
  for (auto& b : c.music) b = uint8_t(rng());
  c.palette[5] = 0x12;
  c.label.assign(addr::kScreenSize, 0);
  for (auto& b : c.label) b = uint8_t(rng() % 32);
  return c;
}

void check_same(const Cart& a, const Cart& b) {
  CHECK(a.code == b.code);
  CHECK(a.get_meta("title") == b.get_meta("title"));
  CHECK(a.get_meta("author") == b.get_meta("author"));
  CHECK(a.spec_version() == b.spec_version());
  CHECK(a.gfx == b.gfx);
  CHECK(a.flags == b.flags);
  CHECK(a.map == b.map);
  CHECK(a.sfx == b.sfx);
  CHECK(a.wave == b.wave);
  CHECK(a.music == b.music);
  CHECK(a.palette == b.palette);
  CHECK(a.label == b.label);
  CHECK(a.extra == b.extra);
}

}  // namespace

TEST_CASE("text carts round-trip every section") {
  // CRLF line endings are normalized on load, so carts survive Windows editors.
  Cart a = random_cart(1);
  a.extra.emplace_back("future_section", "some data\nmore data\n");
  Cart b;
  auto r = from_text(to_text(a), b);
  REQUIRE_MESSAGE(r.ok, r.error);
  check_same(a, b);
}

TEST_CASE("blank carts serialize to a few lines") {
  Cart c;
  c.code = "print('hello')";
  auto text = to_text(c);
  CHECK(text == "glimmer cart 1\n[meta]\nspec = 1\n[code]\nprint('hello')\n");
  Cart back;
  REQUIRE(from_text(text, back).ok);
  check_same(c, back);
}

TEST_CASE("code without a trailing newline keeps its exact contents") {
  for (std::string code : {"a", "a\n", "a\n\n", "\n", "  "}) {
    Cart c;
    c.code = code;
    Cart back;
    REQUIRE(from_text(to_text(c), back).ok);
    CHECK(back.code == code);
  }
}

TEST_CASE("malformed text carts report the offending line") {
  Cart c;
  auto r = from_text("glimmer cart 1\n[gfx]\nzz\n", c);
  CHECK_FALSE(r.ok);
  CHECK(r.error.find("line 3") != std::string::npos);
  CHECK_FALSE(from_text("hello world", c).ok);
}

TEST_CASE("packed carts round-trip and compress well") {
  Cart a = random_cart(2);
  auto packed = pack(a);
  Cart b;
  auto r = unpack(packed, b);
  REQUIRE_MESSAGE(r.ok, r.error);
  check_same(a, b);

  Cart blank;
  CHECK(pack(blank).size() < 1000);
}

TEST_CASE("PNG carts carry the full cart in their pixels") {
  Cart a = random_cart(3);
  // Random gfx doesn't compress; keep it small enough for the PNG capacity.
  std::fill(a.gfx.begin() + 4096, a.gfx.end(), 0);
  std::fill(a.label.begin() + 2048, a.label.end(), 3);
  std::vector<uint8_t> png;
  auto r = to_png(a, png);
  REQUIRE_MESSAGE(r.ok, r.error);

  Image img;
  REQUIRE(decode_png(png, img));
  CHECK(img.w == kPngWidth);
  CHECK(img.h == kPngHeight);

  Cart b;
  auto lr = load_bytes(png, b);
  REQUIRE_MESSAGE(lr.ok, lr.error);
  check_same(a, b);
}

TEST_CASE("damaged PNG carts are rejected") {
  Cart a;
  a.code = "x=1";
  std::vector<uint8_t> png;
  REQUIRE(to_png(a, png).ok);
  Image img;
  REQUIRE(decode_png(png, img));
  img.rgba[(12 + 3) * 4] ^= 1;  // flip a bit inside the payload
  Cart b;
  auto r = from_png(encode_png(img), b);
  CHECK_FALSE(r.ok);
  CHECK(r.error.find("damaged") != std::string::npos);
}

TEST_CASE("oversized carts refuse PNG export instead of truncating") {
  Cart a = random_cart(4);
  std::mt19937 rng(4);
  for (auto& b : a.map) b = uint8_t(rng());
  std::vector<uint8_t> png;
  auto r = to_png(a, png);
  CHECK_FALSE(r.ok);
}

TEST_CASE("carts load into and capture from console memory") {
  Cart a = random_cart(5);
  Memory mem;
  a.load_into(mem);
  CHECK(mem.peek(addr::kGfx + 10) == a.gfx[10]);
  CHECK(mem.peek(addr::kSfx + 3) == a.sfx[3]);
  Cart b;
  b.capture_from(mem);
  CHECK(b.gfx == a.gfx);
  CHECK(b.map == a.map);
  CHECK(b.palette == a.palette);
}
