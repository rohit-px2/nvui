#ifndef NVUI_POPUPMENU_HPP
#define NVUI_POPUPMENU_HPP
#include <QWidget>
#include <QCompleter>
#include <QPaintEvent>
#include <msgpack.hpp>
#include "hlstate.hpp"

struct PMenuItem
{
  bool selected = false;
  QString word;
  QString kind;
  QString menu;
  QString info;
};

class PopupMenu : public QWidget
{
public:
  PopupMenu(const HLState* state, QWidget* parent = nullptr)
    : QWidget(parent),
      hl_state(state),
      pixmap(),
      completion_items(max_items),
      pmenu_font()
  {
    // Without this flag, flickering occurs on WinEditorArea.
    setAttribute(Qt::WA_NativeWindow);
  }
  /**
   * Handles a Neovim "popupmenu_show" event, showing the popupmenu with the
   * given items.
   */
  void pum_show(const msgpack::object* obj, std::uint32_t size);
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
  /**
   * Update the highlight attributes used for drawing the popup menu.
   */
  void update_highlight_attributes();
  void font_changed(const QFont& font, float cell_width, float cell_height, int linespace);
  inline bool hidden() { return is_hidden; }
  /**
   * Returns the internal pixmap.
   * NOTE: You should only draw the rectangle given by
   * available_rect() since the rest might be uninitialized.
   */
  inline const QPixmap& as_pixmap() const noexcept
  {
    return pixmap;
  }
  inline QRect available_rect() const noexcept
  {
    return QRect(
      0, 0, 
      max_chars * cell_width + border_width * 2,
      std::min(completion_items.size(), max_items) * cell_height + border_width * 2
    );
  }
  auto position() const noexcept
  {
    return std::make_tuple(grid_num, row, col);
  }

  inline float outline_width() const noexcept
  {
    return border_width;
  }

  inline void set_outline_width(float outline_width)
  {
    border_width = outline_width;
  }
private:
  /**
   * Add the given popupmenu items to the popup menu.
   */
  void add_items(const msgpack::object_array& items);
  /**
   * Redraw the popupmenu.
   */
  void paint();
  /**
   * Draw the given popup menu item starting at the given y-coordinate
   * and with the given attribute.
   */
  void draw_with_attr(QPainter& p, const HLAttr& attr, const PMenuItem& item, int y);
  const HLState* hl_state;
  const HLAttr* pmenu = nullptr;
  const HLAttr* pmenu_sel = nullptr;
  const HLAttr* pmenu_sbar = nullptr;
  const HLAttr* pmenu_thumb = nullptr;
  std::size_t max_chars = 50;
  // Max items to display on screen at once.
  std::size_t max_items = 15;
  int cur_selected = -1;
  QPixmap pixmap;
  float font_ascent = 0.f;
  std::vector<PMenuItem> completion_items;
  float cell_width = 0.f;
  float cell_height = 0.f;
  int grid_num = 0;
  int row = 0;
  int col = 0;
  int linespace = 0;
  bool has_scrollbar = false;
  bool is_hidden = true;
  QFont pmenu_font;
  float border_width = 1.f;
  Q_OBJECT
protected:
  void paintEvent(QPaintEvent* event) override;
};

#endif // NVUI_POPUPMENU_HPP
