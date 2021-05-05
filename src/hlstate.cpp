#include "hlstate.hpp"
#include <cassert>

namespace hl
{
  HLAttr hl_attr_from_object(const msgpack::object& obj)
  {
    assert(obj.type == msgpack::type::ARRAY);
    const msgpack::object_array& arr = obj.via.array;
    assert(arr.size == 4);
    const int id = arr.ptr[0].as<int>();
    assert(arr.ptr[1].type == msgpack::type::MAP);
    const msgpack::object_map& opts = arr.ptr[1].via.map;
    // We ignore arr.ptr[2] since that's the cterm_attr,
    // we only use rgb_attr
    HLAttr attr {id};
    // Add options
    for(std::uint32_t i = 0; i < opts.size; ++i)
    {
      const auto& kv = opts.ptr[i];
      // Keys are strings, vals could be bools or ints
      assert(kv.key.type == msgpack::type::STR);
      const std::string k = kv.key.as<std::string>();
      switch(kv.val.type)
      {
        case msgpack::type::BOOLEAN:
        {
          attr.font_opts[k] = kv.val.as<bool>();
          break;
        }
        case msgpack::type::POSITIVE_INTEGER:
        {
          attr.color_opts[k] = kv.val.as<int>();
          break;
        }
        default:
        {
          assert(!"hl_attr_define: invalid msgpack val type");
        }
      }
    }
    // Add info
    assert(arr.ptr[3].type == msgpack::type::ARRAY);
    assert(arr.ptr[3].via.array.ptr[0].type == msgpack::type::MAP);
    const msgpack::object_map& info = arr.ptr[3].via.array.ptr[0].via.map;
    for(std::uint32_t i = 0; i < info.size; ++i)
    {
      const auto& kv = info.ptr[i];
      assert(kv.key.type == msgpack::type::STR);
      const std::string k = kv.key.as<std::string>();
      switch(kv.val.type)
      {
        case msgpack::type::STR:
        {
          attr.info[k] = kv.val.as<std::string>();
          break;
        }
        default:
        {
          break;
        }
      }
    }
    return attr;
  }
}

HLAttr::HLAttr()
: hl_id(0),
  font_opts(),
  color_opts(),
  info() {}

HLAttr::HLAttr(int id)
: hl_id(id),
  font_opts(),
  color_opts(),
  info() {}

HLAttr::HLAttr(const HLAttr& other)
{
  hl_id = other.hl_id;
  font_opts = other.font_opts;
  color_opts = other.color_opts;
  info = other.info;
}
