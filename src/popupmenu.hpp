#ifndef NVUI_POPUPMENU_HPP
#define NVUI_POPUPMENU_HPP
#include <QWidget>
#include <msgpack.hpp>
#include "hlstate.hpp"

class PopupMenu : public QWidget
{
public:
  PopupMenu(const HLState* state)
    : hl_state(state) {}
  /**
   * Handles a Neovim "popupmenu_show" event, showing the popupmenu with the
   * given items.
   */
  void pum_show(const msgpack::object* obj, std::uint32_t size = 1);
  /**
   * Hides the popupmenu.
   */
  void pum_hide(const msgpack::object* obj, std::uint32_t size = 1);
  /**
   * Handles a Neovim "popupmenu_select" event, selecting the
   * popupmenu item at the given index,
   * or selecting nothing if the index is -1.
   */
  void pum_sel(const msgpack::object* obj, std::uint32_t size = 1);
private:
  /**
   * Add the given popupmenu items to the popup menu.
   */
  void add_items(const msgpack::object_array& items);
  const HLState* hl_state;
  const HLAttr* pmenu = nullptr;
  const HLAttr* pmenusel = nullptr;
  const HLAttr* pmenusbar = nullptr;
  const HLAttr* pmenuthumb = nullptr;
  bool has_scrollbar = false;
  bool hidden = true;
  Q_OBJECT
};

#endif // NVUI_POPUPMENU_HPP
