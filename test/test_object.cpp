#include <msgpack.hpp>
#include "object.hpp"
#include <catch2/catch.hpp>
#include <iostream>

template<typename T>
msgpack::object_handle pack(const T& t)
{
  std::stringstream ss;
  msgpack::pack(ss, t);
  const auto& s = ss.str();
  return msgpack::unpack(s.data(), s.size());
}

TEST_CASE("Object parsing works", "[Object]")
{
  const auto test_primitive = [&](auto prim, auto type) {
    auto o = msgpack::object(prim);
    REQUIRE(o.type == type);
    auto parsed = Object::parse(o);
    using prim_type = std::remove_reference_t<decltype(prim)>;
    REQUIRE(std::holds_alternative<prim_type>(parsed));
    REQUIRE(std::get<decltype(prim)>(parsed) == prim);
  };
  SECTION("Primitives")
  {
    test_primitive(uint64_t(5), msgpack::type::POSITIVE_INTEGER);
    test_primitive(int64_t(-1), msgpack::type::NEGATIVE_INTEGER);
    test_primitive(false, msgpack::type::BOOLEAN);
    test_primitive(true, msgpack::type::BOOLEAN);
    test_primitive(double(42.3), msgpack::type::FLOAT64);
    std::string s = "hello";
    auto oh = pack(s);
    const auto& o = oh.get();
    auto parsed = Object::parse(o);
    REQUIRE(std::holds_alternative<QString>(parsed));
    REQUIRE(std::get<QString>(parsed) == s.c_str());
  }
  SECTION("Arrays")
  {
    {
      std::vector<int64_t> v {-1, -2, -3, -4};
      std::stringstream ss;
      msgpack::pack(ss, v);
      auto oh = msgpack::unpack(ss.str().data(), ss.str().size());
      const auto& o = oh.get();
      REQUIRE(o.type == msgpack::type::ARRAY);
      REQUIRE(o.via.array.size == v.size());
      auto parsed = Object::parse(o);
      REQUIRE(std::holds_alternative<ObjectArray>(parsed));
      const auto& omap = std::get<ObjectArray>(parsed);
      for(std::size_t i = 0; i < omap.size(); ++i)
      {
        using val_type = decltype(v)::value_type;
        REQUIRE(std::holds_alternative<val_type>(omap.at(i)));
        REQUIRE(std::get<val_type>(omap.at(i)) == v.at(i));
      }
    }
    {
      std::vector<std::string> v {"hello", "hi"};
      std::stringstream ss;
      msgpack::pack(ss, v);
      auto oh = msgpack::unpack(ss.str().data(), ss.str().size());
      const auto& o = oh.get();
      REQUIRE(o.type == msgpack::type::ARRAY);
      REQUIRE(o.via.array.size == v.size());
      auto parsed = Object::parse(o);
      REQUIRE(std::holds_alternative<ObjectArray>(parsed));
      const auto& omap = std::get<ObjectArray>(parsed);
      for(std::size_t i = 0; i < omap.size(); ++i)
      {
        using val_type = QString;
        REQUIRE(std::holds_alternative<val_type>(omap.at(i)));
        REQUIRE(std::get<val_type>(omap.at(i)) == QString::fromStdString(v.at(i)));
      }
    }
  }
  SECTION("Maps")
  {
    using string_map = std::unordered_map<std::string, std::string>;
    string_map mp {
      {"hello", "hi"},
      {"here", "there"}
    };
    std::stringstream ss;
    msgpack::pack(ss, mp);
    auto oh = msgpack::unpack(ss.str().data(), ss.str().size());
    const auto& o = oh.get();
    REQUIRE(o.type == msgpack::type::MAP);
    REQUIRE(o.via.map.size == static_cast<uint32_t>(mp.size()));
    auto parsed = Object::parse(o);
    REQUIRE(std::holds_alternative<ObjectMap>(parsed));
    const auto& omap = std::get<ObjectMap>(parsed);
    REQUIRE(omap.size() == static_cast<uint32_t>(mp.size()));
    for(const auto& [key, val] : omap)
    {
      REQUIRE(mp.contains(key));
      // string type gets parsed to QString
      REQUIRE(std::holds_alternative<QString>(val));
    }
  }
}

TEST_CASE("Extracting types from Object")
{
  SECTION("Implicit integer conversion")
  {
    Object o = std::uint64_t(5);
    REQUIRE(o.has<std::uint64_t>());
    // Should be able to convert to int(5)
    try
    {
      int x = o;
      REQUIRE(x == 5);
    }
    catch(...) { REQUIRE(!"Could not convert o = 5 to int."); }
  }
  SECTION("Get throws exception if it doesn't work")
  {
    Object o = std::uint64_t(5);
    bool caught = false;
    try
    {
      std::string s = o;
      REQUIRE(false);
    }
    catch(...) { caught = true; }
    REQUIRE(caught);
  }
  SECTION("Optional function works")
  {
    Object o = QString("hello world");
    auto qstr_opt = o.get_as<QString>();
    REQUIRE(qstr_opt);
    REQUIRE(*qstr_opt == "hello world");
  }
  SECTION("Optional function returns nullopt if could not convert")
  {
    Object o = std::uint64_t(5);
    // Optional returns nullopt if not convertible
    auto str_opt = o.get_as<std::string>();
    REQUIRE(!str_opt);
  }
}
