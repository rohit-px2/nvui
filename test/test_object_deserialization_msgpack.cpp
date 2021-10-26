#include <catch2/catch.hpp>
#include "object.hpp"
#include <msgpack.hpp>
#include <cstdint>

TEST_CASE("Object correctly deserialies from msgpack")
{
  SECTION("Deserializing an array of u64s")
  {
    msgpack::sbuffer pack_buf;
    std::vector<std::uint64_t> x {5, 2, 3, 4, 6};
    msgpack::pack(pack_buf, x);
    std::string_view sv {pack_buf.data(), pack_buf.size()};
    std::size_t offset = 0;
    auto parsed = Object::from_msgpack(sv, offset);
    REQUIRE(offset == sv.size()); // Only one object
    REQUIRE(parsed.has<ObjectArray>());
    REQUIRE(parsed.array()->size() == x.size());
    const auto& arr = parsed.get<ObjectArray>();
    for(std::size_t i = 0; i < arr.size(); ++i)
    {
      auto opt = arr[i].try_convert<decltype(x)::value_type>();
      REQUIRE(opt);
      REQUIRE(*opt == x[i]);
    }
  }
  SECTION("Error parsing")
  {
    msgpack::sbuffer sbuf;
    std::vector<std::uint64_t> x {1, 2, 3, 4, 5, 6};
    msgpack::pack(sbuf, x);
    // Reducing the size of the string view by 1
    // causes it to not read the "array end" message,
    // causing an error (it tries to read more elements).
    std::string_view sv(sbuf.data(), sbuf.size() - 1);
    std::size_t offset = 0;
    Object o = Object::from_msgpack(sv, offset);
    REQUIRE(o.has_err());
    REQUIRE(o.get<Error>().msg == "Insufficient Bytes");
  }
}
