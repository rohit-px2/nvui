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
      if (k == "foreground")
      {
        attr.foreground = Color(kv.val.as<std::uint32_t>());
      }
      else if (k == "background")
      {
        attr.background = Color(kv.val.as<std::uint32_t>());
      }
      else if (k == "reverse")
      {
        attr.reverse = kv.val.as<bool>();
      }
      else if (k == "special")
      {
        attr.special = Color(kv.val.as<std::uint32_t>());
      }
      else if (k == "italic")
      {
        attr.font_opts |= FontOpts::Italic * kv.val.as<bool>();
      } 
      else if (k == "bold")
      {
        attr.font_opts |= FontOpts::Bold * kv.val.as<bool>();
      }
      else if (k == "underline")
      {
        attr.font_opts |= FontOpts::Underline * kv.val.as<bool>();
      }
      else if (k == "strikethrough")
      {
        attr.font_opts |= FontOpts::Strikethrough * kv.val.as<bool>();
      }
      else if (k == "undercurl")
      {
        attr.font_opts |= FontOpts::Undercurl;
      }
    }
    // Add info
    assert(arr.ptr[3].type == msgpack::type::ARRAY);
    const msgpack::object_array& info_arr = arr.ptr[3].via.array;
    for(std::uint32_t i = 0; i < info_arr.size; ++i)
    {
      AttrState state;
      assert(info_arr.ptr[i].type == msgpack::type::MAP);
      const msgpack::object_map& mp = info_arr.ptr[i].via.map;
      for(std::uint32_t j = 0; j < mp.size; ++j)
      {
        const msgpack::object_kv& kv = mp.ptr[j];
        assert(kv.key.type == msgpack::type::STR);
        const std::string k = kv.key.as<std::string>();
        if (k == "kind")
        {
          state.kind = kv.val.as<std::string>() == "syntax" ? Kind::Syntax : Kind::UI;
        }
        else if (k == "hi_name")
        {
          state.hi_name = kv.val.as<decltype(state.hi_name)>();
        }
        else if (k == "ui_name")
        {
          state.ui_name = kv.val.as<decltype(state.ui_name)>();
        }
        else if (k == "id")
        {
          state.id = kv.val.as<decltype(state.id)>();
        }
      }
      attr.state.push_back(std::move(state));
    }
    return attr;
  }
}

/*
   HLAttr Implementation
*/

HLAttr::HLAttr()
: hl_id(0) {}

HLAttr::HLAttr(int id)
: hl_id(id) {}

HLAttr::HLAttr(const HLAttr& other)
: hl_id(other.hl_id) {}

HLAttr::HLAttr(HLAttr&& other)
: hl_id(other.hl_id),
  reverse(other.reverse),
  special(other.special),
  foreground(other.foreground),
  background(other.background),
  state(std::move(other.state)),
  opacity(other.opacity) {}

HLAttr& HLAttr::operator=(HLAttr&& other)
{
  hl_id = other.hl_id;
  foreground = other.foreground;
  background = other.background;
  special = other.special;
  reverse = other.reverse;
  state = std::move(other.state);
  opacity = other.opacity;
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
  default_colors.foreground = params.ptr[0].as<std::uint32_t>();
  default_colors.background = params.ptr[1].as<std::uint32_t>();
  default_colors.special = params.ptr[2].as<std::uint32_t>();
}


const HLAttr& HLState::default_colors_get() const
{
  return default_colors;
}


void HLState::define(const msgpack::object& obj)
{
  HLAttr attr = hl::hl_attr_from_object(obj);
  int id = attr.hl_id;
  for(const AttrState& s : attr.state)
  {
    set_name_id(s.hi_name, id);
  }
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
