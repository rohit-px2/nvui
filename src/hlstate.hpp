#ifndef NVUI_HLSTATE_HPP
#define NVUI_HLSTATE_HPP

#include <unordered_map>
#include <variant>
#include <vector>
#include <string>
#include <msgpack.hpp>

using hval = std::variant<int, bool>;

/// Data for a single highlight attribute
class HLAttr
{
public:
  int hl_id;
  std::unordered_map<std::string, bool> font_opts {
    {"underline", false},
    {"italic", false},
    {"undercurl", false},
    {"bold", false},
    {"strikethrough", false}
  };
  std::unordered_map<std::string, int> color_opts;
  /// Keeps keys "kind", "ui_name", "hi_name"
  std::unordered_map<std::string, std::string> info;
  HLAttr();
  HLAttr(int id);
  HLAttr(const HLAttr& other);
};

/// Keeps the highlight state of Neovim
/// HlState is essentially a map of highlight names to their
/// corresponding id's, and a secondary map of id's to
/// the HLAttr they correspond to.
class HLState
{
public:
  HLState() = default;
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
  void define(const msgpack::object& obj);
  /**
   * Sets the default colors.
   */
  void default_colors_set(const msgpack::object& obj);
  const HLAttr& default_colors_get() const;
private:
  HLAttr default_colors;
  std::unordered_map<std::string, std::uint32_t> name_to_id;
  std::unordered_map<int, HLAttr> id_to_attr;
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
  HLAttr hl_attr_from_object(const msgpack::object& obj);
}

#endif
