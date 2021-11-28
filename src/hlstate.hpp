#ifndef NVUI_HLSTATE_HPP
#define NVUI_HLSTATE_HPP

#include <cstdint>
#include <iostream>
#include <QFont>
#include <unordered_map>
#include <variant>
#include <optional>
#include <vector>
#include <string>
#include <type_traits>
#include <QColor>
#include "object.hpp"

using uint8 = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;

enum Kind
{
  Syntax,
  UI
};

struct Color
{
  uint8 r;
  uint8 g;
  uint8 b;
  Color() = default;
  Color(uint32 clr)
    : r((clr & 0x00ff0000) >> 16),
      g((clr & 0x0000ff00) >> 8),
      b((clr & 0x000000ff))
  {
  }
  Color(int clr) : Color(static_cast<uint32>(clr)) {}
  Color(uint64 clr) : Color(static_cast<uint32>(clr)) {}
  /**
   * Converts a Color back to a uint32
   */
  uint32 to_uint32() const
  {
    return r << 16 | g << 8 | b;
  }

  QColor qcolor() const
  {
    return {r, g, b};
  }
  
  bool operator==(const Color& other) const
  {
    return r == other.r && b == other.b && g == other.g;
  }
private:
};

struct AttrState
{
  Kind kind;
  std::string hi_name;
  std::string ui_name;
  int id;
};

enum FontOpts : std::uint8_t
{
  Normal = 1,
  Bold = 2,
  Italic = 4,
  Underline = 16,
  Strikethrough = 32,
  Undercurl = 64
};

using FontOptions = std::underlying_type_t<FontOpts>;

/// Data for a single highlight attribute
class HLAttr
{
public:
  struct ColorPair
  {
    Color fg;
    Color bg;
  };
  struct ColorTriplet
  {
    Color fg;
    Color bg;
    Color sp;
  };
  std::optional<Color> fg() const { return foreground; }
  std::optional<Color> bg() const { return background; }
  std::optional<Color> sp() const { return special; }
  bool italic() const { return font_opts & FontOpts::Italic; }
  bool bold() const { return font_opts & FontOpts::Bold; }
  bool strikethrough() const { return font_opts & FontOpts::Strikethrough; }
  bool underline() const { return font_opts & FontOpts::Underline; }
  bool undercurl() const { return font_opts & FontOpts::Undercurl; }
  ColorPair fg_bg(const HLAttr& fallback) const
  {
    ColorPair cp = {
      foreground.value_or(fallback.foreground.value()),
      background.value_or(fallback.background.value())
    };
    if (reverse) std::swap(cp.fg, cp.bg);
    return cp;
  }
  ColorTriplet fg_bg_sp(const HLAttr& fallback) const
  {
    auto&& [fg, bg] = fg_bg(fallback);
    return {
      fg, bg, special.value_or(fg)
    };
  }
  int hl_id = 0;
  FontOptions font_opts = FontOpts::Normal;
  bool reverse = false;
  std::optional<Color> special {};
  std::optional<Color> foreground {};
  std::optional<Color> background {};
  /// We don't need a detailed view of the highlight state
  // right now so we won't do anything with this.
  std::vector<AttrState> state {};
  float opacity = 1;
};

/// Keeps the highlight state of Neovim
/// HlState is essentially a map of highlight names to their
/// corresponding id's, and a secondary map of id's to
/// the HLAttr they correspond to.
class HLState
{
public:
  HLState() {
    id_to_attr.reserve(1000);
  }
  /**
   * Maps name to hl_id.
   * This function maps to "hl_group_set".
   */
  void set_name_id(const std::string& name, std::uint32_t hl_id);
  /**
   * Maps id to attr.
   */
  void set_id_attr(int id, HLAttr attr);
  /**
   * Returns the highlight attribute for the given id.
   */
  const HLAttr& attr_for_id(int id) const;
  /**
   * Returns the name of the highlight group for the given id.
   */
  int id_for_name(const std::string& name) const;
  /**
   * Manages an "hl_attr_define" call, with obj
   * being the parameters of the call.
   */
  void define(const Object& obj);
  /**
   * Sets the default colors.
   */
  void default_colors_set(const Object& obj);
  /**
   * Sets the given highlight group. This should be called with
   * the parameters of an "hl_group_set" call.
   */
  void group_set(const Object& obj);
  /**
   * Returns the default colors.
   */
  const HLAttr& default_colors_get() const;
  Color default_bg() const { return default_colors.bg().value(); }
  Color default_fg() const { return default_colors.fg().value(); }
  HLAttr::ColorPair colors_for(const HLAttr& attr) const;
private:
  HLAttr default_colors;
  std::unordered_map<std::string, std::uint32_t> name_to_id;
  //std::unordered_map<int, HLAttr> id_to_attr;
  std::vector<HLAttr> id_to_attr {1};
};

/// Defining a function to parse "hl_attr_define" data
/// into an HLState (for startup, after that
/// we modify the initial state).
namespace hl
{
  /**
   * Produces an HLAttr from the given object.
   * obj must be of type msgpack::type::ARRAY,
   * and should only be called with arrays
   * that were the parameters of an "hl_attr_define"
   * call.
   */
  HLAttr hl_attr_from_object(const Object& obj);
}

namespace font
{
  template<bool set_stul = true>
  void set_opts(QFont& font, const FontOptions options);
}

#endif
