#include "hlstate.hpp"
#include "utils.hpp"
#include <cassert>
#include <iostream>
#include <sstream>
namespace hl
{
  HLAttr hl_attr_from_object(const Object& obj)
  {
    using u32 = std::uint32_t;
    const auto* arr = obj.array();
    assert(arr && arr->size() >= 4);
    const int id = static_cast<int>(*arr->at(0).u64());
    auto* map_ptr = arr->at(1).map();
    if (!map_ptr) return {};
    const ObjectMap& map = *map_ptr;
    HLAttr attr {id};
    if (map.contains("foreground"))
    {
      attr.foreground = static_cast<u32>(*map.at("foreground").u64());
    }
    if (map.contains("background"))
    {
      attr.background = static_cast<u32>(*map.at("background").u64());
    }
    if (map.contains("reverse")) attr.reverse = true;
    if (map.contains("special"))
    {
      attr.special = static_cast<u32>(*map.at("special").u64());
    }
    if (map.contains("italic")) attr.font_opts |= FontOpts::Italic;
    if (map.contains("bold")) attr.font_opts |= FontOpts::Bold;
    if (map.contains("underline")) attr.font_opts |= FontOpts::Underline;
    if (map.contains("strikethrough"))
    {
      attr.font_opts |= FontOpts::Strikethrough;
    }
    if (map.contains("undercurl")) attr.font_opts |= FontOpts::Undercurl;
    auto* info_arr = arr->at(3).array();
    if (!arr) return attr;
    for(const auto& o : *info_arr)
    {
      AttrState state;
      auto* state_map = o.map();
      if (!state_map) continue;
      if (state_map->contains("hi_name"))
      {
        auto* hi_name_qstr = state_map->at("hi_name").string();
        assert(hi_name_qstr);
        state.hi_name = hi_name_qstr->toStdString();
      }
      if (state_map->contains("ui_name"))
      {
        auto* qstr = state_map->at("ui_name").string();
        assert(qstr);
        state.ui_name = qstr->toStdString();
      }
      if (state_map->contains("kind"))
      {
        state.hi_name = *state_map->at("kind").string() == "syntax"
          ? Kind::Syntax
          : Kind::UI;
      }
      if (state_map->contains("id"))
      {
        auto* id_ptr = state_map->at("id").u64();
        assert(id_ptr);
        state.id = *id_ptr;
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
: hl_id(other.hl_id),
  reverse(other.reverse),
  special(other.special),
  foreground(other.foreground),
  background(other.background),
  state(other.state),
  opacity(other.opacity) {}

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
  id_to_attr[id] = attr;
}


void HLState::default_colors_set(Object& obj)
{
  // We only look at the first three values (the others are ctermfg
  // and ctermbg, which we don't care about)
  auto* arr = obj.array();
  if (!arr || arr->size() < 3) return;
  auto fg = arr->at(0).get_as<uint32>();
  auto bg = arr->at(1).get_as<uint32>();
  auto sp = arr->at(2).get_as<uint32>();
  assert(fg && bg && sp);
  default_colors.foreground = *fg;
  default_colors.background = *bg;
  default_colors.special = *sp;
}


const HLAttr& HLState::default_colors_get() const
{
  return default_colors;
}


void HLState::define(Object& obj)
{
  HLAttr attr = hl::hl_attr_from_object(obj);
  int id = attr.hl_id;
  for(const AttrState& s : attr.state)
  {
    if (!s.hi_name.empty())
    {
      set_name_id(s.hi_name, id);
    }
    if (!s.ui_name.empty())
    {
      set_name_id(s.ui_name, id);
    }
  }
  id_to_attr[id] = std::move(attr);
}


void HLState::group_set(Object& obj)
{
  auto* arr = obj.array();
  assert(arr && arr->size() >= 2);
  auto* name = arr->at(0).string();
  auto* id = arr->at(1).u64();
  if (!name || !id) return;
  auto hl_name = name->toStdString();
  auto hl_id = static_cast<int>(*id);
  set_name_id(std::move(hl_name), hl_id);
}
