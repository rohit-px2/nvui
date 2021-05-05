#include "hlstate.hpp"
#include <catch2/catch.hpp>
#include <msgpack.hpp>
#include <sstream>
TEST_CASE("hl_attr_from_object works", "[hl_attr_from_object]")
{
  SECTION("Works for standard data examples taken from Neovim messages")
  {
    // TODO: Save data from Neovim messages so that we can reuse them in tests.
    // Writing a bunch of pack commands is not very fun.
    std::stringstream ss;
    msgpack::packer<std::stringstream> packer {ss};
    packer.pack_array(4);
    packer.pack_int32(107);
    packer.pack_map(2);
    packer.pack("italic");
    packer.pack(true);
    packer.pack("foreground");
    packer.pack(16753826);
    packer.pack_map(0);
    packer.pack_array(1);
    packer.pack_map(3);
    packer.pack("kind");
    packer.pack("syntax");
    packer.pack("hi_name");
    packer.pack("TSParameter");
    packer.pack("id");
    packer.pack(107);
    const auto oh = msgpack::unpack(ss.str().data(), ss.str().size());
    const auto& obj = oh.get();
    const HLAttr resulting_attr = hl::hl_attr_from_object(obj);
    REQUIRE(resulting_attr.hl_id == 107);
    REQUIRE(resulting_attr.info == decltype(resulting_attr.info) {
      {"kind", "syntax"},
      {"hi_name", "TSParameter"}
    });
    REQUIRE(resulting_attr.color_opts == decltype(resulting_attr.color_opts) {
      {"foreground", 16753826}
    });
    REQUIRE(resulting_attr.font_opts == decltype(resulting_attr.font_opts) {
      {"italic", true}
    });
  }
}
