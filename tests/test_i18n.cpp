#include <doctest.h>

#include "ui/i18n.hpp"

using namespace glim::ui;

TEST_CASE("translations fall back to english and fill placeholders") {
  REQUIRE(set_language("en"));
  CHECK(tr("saved {0}", "a.glim") == "saved a.glim");
  load_strings("xx", "# test\nlanguage = Test\nsaved {0} = {0} kept\nhello = howdy\nbroken line\n");
  REQUIRE(set_language("xx"));
  CHECK(tr("saved {0}", "a.glim") == "a.glim kept");
  CHECK(tr("hello") == "howdy");
  CHECK(tr("not translated") == "not translated");
  CHECK_FALSE(set_language("nope"));
  CHECK(language() == "xx");
  set_language("en");
}

TEST_CASE("albanian ships with the console") {
  bool found = false;
  for (const auto& [code, name] : languages()) found |= code == "sq" && name == "Shqip";
  CHECK(found);
  REQUIRE(set_language("sq"));
  CHECK(tr("saved {0}", "x") == "u ruajt x");
  set_language("en");
}
