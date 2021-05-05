#ifndef NVUI_HLSTATE_HPP
#define NVUI_HLSTATE_HPP

#include <unordered_map>
#include <variant>
#include <vector>
#include <string>
#include <msgpack.hpp>

using std::unordered_map;
using std::vector;
using std::string;
using hval = std::variant<int, bool>;

/// Data for a single highlight attribute
struct HLAttr
{
  int hl_id;
  unordered_map<string, bool> font_opts;
  unordered_map<string, int> color_opts;
  /// Keeps keys "kind", "ui_name", "hi_name"
  unordered_map<string, string> info;
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
  /**
   * Maps name to hl_id.
   * This function maps to "hl_group_set".
   */
  void set_name_id(const std::string& name, std::uint32_t hl_id)
  {
    name_to_id[name] = hl_id;
  }
  void set_id_attr(int id, HLAttr attr)
  {
    id_to_attr[id] = attr;
  }
  HLAttr& attr_for_id(int id)
  {
    return id_to_attr[id];
  }
  int id_for_name(const std::string& name)
  {
    return name_to_id[name];
  }
private:
  unordered_map<string, std::uint32_t> name_to_id;
  unordered_map<int, HLAttr> id_to_attr;
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
