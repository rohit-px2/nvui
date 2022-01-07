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
    assert(arr->at(0).convertible<int>());
    const int id = (int) arr->at(0);
    auto* map_ptr = arr->at(1).map();
    if (!map_ptr) return {};
    const ObjectMap& map = *map_ptr;
    HLAttr attr {id};
    if (map.contains("foreground"))
    {
      assert(map.at("foreground").convertible<u32>());
      attr.foreground = (u32) map.at("foreground");
    }
    if (map.contains("background"))
    {
      assert(map.at("background").convertible<u32>());
      attr.background = (u32) map.at("background");
    }
    if (map.contains("reverse")) attr.reverse = true;
    if (map.contains("special"))
    {
      assert(map.at("special").convertible<u32>());
      attr.special = (u32) map.at("special");
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
      if (auto* hi_name = o.try_at("hi_name").string())
      {
        state.hi_name = *hi_name;
      }
      if (auto* ui_name = o.try_at("ui_name").string())
      {
        state.ui_name = *ui_name;
      }
      if (auto* kind = o.try_at("kind").string())
      {
        state.hi_name = *kind == "syntax"
          ? Kind::Syntax
          : Kind::UI;
      }
      if (auto hid = o.try_at("id").try_convert<int>())
      {
        state.id = hid.value();
      }
      attr.state.push_back(std::move(state));
    }
    return attr;
  }
} // namespace hl

namespace font
{
  template<>
  void set_opts<true>(QFont& font, const FontOptions opts)
  {
    font.setItalic(opts & FontOpts::Italic);
    font.setBold(opts & FontOpts::Bold);
    font.setStrikeOut(opts & FontOpts::Strikethrough);
    font.setUnderline(opts & FontOpts::Underline);
  }
  template<>
  void set_opts<false>(QFont& font, const FontOptions opts)
  {
    font.setItalic(opts & FontOpts::Italic);
    font.setBold(opts & FontOpts::Bold);
  }
} // namespace font

const HLAttr& HLState::attr_for_id(int id) const
{
  if (id < 0 || id >= (int) id_to_attr.size()) return default_colors;
  return id_to_attr[id];
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
  if (id > (int) id_to_attr.size())
  {
    // Shouldn't happen with the way Neovim gives us highlight
    // attributes but just to make sure
    id_to_attr.resize((std::size_t) id + 1);
  }
  if (id == (int) id_to_attr.size())
  {
    id_to_attr.emplace_back(std::move(attr));
    return;
  }
  id_to_attr[id] = std::move(attr);
}

void HLState::default_colors_set(const Object& obj)
{
  // We only look at the first three values (the others are ctermfg
  // and ctermbg, which we don't care about)
  auto* arr = obj.array();
  if (!arr || arr->size() < 3) return;
  auto fg = arr->at(0).try_convert<uint32>();
  auto bg = arr->at(1).try_convert<uint32>();
  auto sp = arr->at(2).try_convert<uint32>();
  assert(fg && bg && sp);
  default_colors.foreground = *fg;
  default_colors.background = *bg;
  default_colors.special = *sp;
}

const HLAttr& HLState::default_colors_get() const
{
  return default_colors;
}

void HLState::define(const Object& obj)
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
  set_id_attr(attr.hl_id, std::move(attr));
}

void HLState::group_set(const Object& obj)
{
  auto* arr = obj.array();
  assert(arr && arr->size() >= 2);
  auto* name = arr->at(0).string();
  auto* id = arr->at(1).u64();
  if (!name || !id) return;
  auto hl_name = *name;
  auto hl_id = static_cast<int>(*id);
  set_name_id(std::move(hl_name), hl_id);
}

HLAttr::ColorPair HLState::colors_for(const HLAttr& attr) const
{
  return attr.fg_bg(default_colors);
}
