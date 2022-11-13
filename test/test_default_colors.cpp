#include <catch2/catch_all.hpp>
#include <sstream>
#include "hlstate.hpp"
#include "object.hpp"
#include <unordered_map>

TEST_CASE("default_colors are set properly", "[default_colors_set]")
{
  using namespace std::string_literals;
  HLState hl_state;
  // Params of "default_colors_set"
  SECTION("default_colors_set: Test #1")
  {
    std::stringstream ss;
    msgpack::packer<std::stringstream> packer {ss};
    packer.pack_array(5);
    packer.pack(16777215); // rgb fg
    packer.pack(0); // rgb bg
    packer.pack(16711680); // rgb special
    packer.pack(0); // cterm fg
    packer.pack(0); // cterm bg
    const auto oh = msgpack::unpack(ss.str().data(), ss.str().size());
    const msgpack::object& obj = oh.get();
    auto parsed = Object::parse(obj);
    hl_state.default_colors_set(parsed);
    const auto& def = hl_state.default_colors_get();
    REQUIRE(def.fg());
    REQUIRE(def.bg());
    REQUIRE(def.sp());
    REQUIRE(def.fg()->to_uint32() == 16777215);
    REQUIRE(def.bg()->to_uint32() == 0);
    REQUIRE(def.sp()->to_uint32() == 16711680);
  }
}
