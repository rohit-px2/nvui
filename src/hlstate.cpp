#include "hlstate.hpp"
#include <cassert>
#include <iostream>

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

/*
   HLAttr Implementation
*/

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
: hl_id(other.hl_id),
  font_opts(other.font_opts),
  color_opts(other.color_opts),
  info(other.info) {}

HLAttr::HLAttr(HLAttr&& other)
: hl_id(other.hl_id),
  font_opts(std::move(other.font_opts)),
  color_opts(std::move(other.color_opts)),
  info(other.info) {}

HLAttr& HLAttr::operator=(HLAttr&& other)
{
  hl_id = other.hl_id;
  font_opts = std::move(other.font_opts);
  color_opts = std::move(other.color_opts);
  info = std::move(other.info);
  return *this;
}

/*
   HLState Implementation
*/


const HLAttr& HLState::attr_for_id(int id) const
{
  const auto it = id_to_attr.find(id);
  if (it != id_to_attr.end())
  {
    return it->second;
  }
  return default_colors;
}


int HLState::id_for_name(const std::string &name) const
{
  const auto it = name_to_id.find(name);
  if (it != name_to_id.end())
  {
    return it->second;
  }
  return 0;
}


void HLState::set_name_id(const std::string& name, std::uint32_t hl_id)
{
  name_to_id[name] = hl_id;
}


void HLState::set_id_attr(int id, HLAttr attr)
{
  id_to_attr[id] = std::move(attr);
}


void HLState::default_colors_set(const msgpack::object& obj)
{
  // We only look at the first three values (the others are ctermfg
  // and ctermbg, which we don't care about)
  assert(obj.type == msgpack::type::ARRAY);
  const msgpack::object_array& params = obj.via.array;
  assert(params.size >= 3);
  HLAttr attr {0}; // 0 for default?
  int foreground = params.ptr[0].as<int>();
  int background = params.ptr[1].as<int>();
  int special = params.ptr[2].as<int>();
  attr.color_opts["foreground"] = foreground;
  attr.color_opts["background"] = background;
  attr.color_opts["special"] = special;
  default_colors = std::move(attr);
}


const HLAttr& HLState::default_colors_get() const
{
  return default_colors;
}


void HLState::define(const msgpack::object& obj)
{
  HLAttr attr = hl::hl_attr_from_object(obj);
  set_id_attr(attr.hl_id, std::move(attr));
}


void HLState::group_set(const msgpack::object &obj)
{
  assert(obj.type == msgpack::type::ARRAY);
  const auto& arr = obj.via.array;
  assert(arr.size == 2);
  assert(arr.ptr[0].type == msgpack::type::STR);
  assert(arr.ptr[1].type == msgpack::type::POSITIVE_INTEGER);
  std::string hl_name = arr.ptr[0].as<std::string>();
  int hl_id = arr.ptr[1].as<int>();
  set_name_id(hl_name, hl_id);
}
