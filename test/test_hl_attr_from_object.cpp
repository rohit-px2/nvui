#include "object.hpp"
#include "hlstate.hpp"
#include <catch2/catch.hpp>
#include <msgpack.hpp>
#include <iostream>
#include <sstream>

TEST_CASE("hl_attr_from_object works", "[hl_attr_from_object]")
{
  SECTION("Works for standard data examples taken from Neovim messages")
  {
    uint64 rgb = 16753826;
    Object o = ObjectArray {
      uint64(107),
      ObjectMap {
        {"italic", true},
        {"foreground", rgb}
      },
      ObjectMap {},
      ObjectArray {
        ObjectMap {
          {"kind", std::string("syntax")},
          {"hi_name", std::string("TSParameter")},
          {"id", uint64(107)}
        }
      }
    };
    const HLAttr resulting_attr = hl::hl_attr_from_object(o);
    REQUIRE(resulting_attr.hl_id == 107);
    REQUIRE(!resulting_attr.bg().has_value());
    REQUIRE(resulting_attr.fg().has_value());
    REQUIRE(resulting_attr.fg().value().to_uint32() == rgb);
  }
}
