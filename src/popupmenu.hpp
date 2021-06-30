#ifndef NVUI_POPUPMENU_HPP
#define NVUI_POPUPMENU_HPP
#include <optional>
#include <QWidget>
#include <QCompleter>
#include <QDebug>
#include <QPaintEvent>
#include <QStringBuilder>
#include <msgpack.hpp>
#include "hlstate.hpp"
#include "utils.hpp"
#include <fmt/core.h>
#include <fmt/format.h>

// popup menu icon filepath
static const QString picon_fp = "../assets/icons/popup/";
/// Manages the popup menu icons and gives the appropriate
/// icon for each popup menu item kind (useful for LSP).
/// Icons are square
class PopupMenuIconManager
{
public:
  using color_opt = std::optional<QColor>;
  using fg_bg = std::pair<color_opt, color_opt>;
  PopupMenuIconManager(int pm_size)
    : sq_width(pm_size)
  {
    load_icons(pm_size);
  }

  void size_changed(int new_size)
  {
    if (sq_width == new_size) return;
    sq_width = new_size;
    load_icons(sq_width);
  }

  const QPixmap* icon_for_kind(const QString& kind);
  inline void set_default_fg(QColor fg) { default_fg = std::move(fg); }
  inline void set_default_bg(QColor bg) { default_bg = std::move(bg); }
  inline void set_bg_for_name(const QString& name, QColor bg)
  {
    colors[name].second = std::move(bg);
  }
  inline void set_fg_for_name(const QString& name, QColor fg)
  {
    colors[name].first = std::move(fg);
  }
private:
  void load_icons(int width);
  QString iname_to_kind(const QString& iname);
  QString kind_to_iname(QString kind);
  // Map string (iname) to (foreground, background) tuple
  // Any color with no value will default to the default_foreground and default_background
  // colors.
  std::unordered_map<QString, fg_bg> colors {
    {"array", {}},
    {"boolean", {}},
    {"class", {}},
    {"color", {}},
    {"constant", {}},
    {"enum-member", {}},
    {"enum", {}},
    {"event", {}},
    {"field", {}},
    {"function", {}},
    {"file", {}},
    {"interface", {}},
    {"key", {}},
    {"keyword", {}},
    {"method", {}},
    {"misc", {}},
    {"namespace", {}},
    {"numeric", {}},
    {"operator", {}},
    {"parameter", {}},
    {"property", {}},
    {"ruler", {}},
    {"snippet", {}},
    {"string", {}},
    {"structure", {}},
    {"variable", {}}
  };
  std::unordered_map<QString, QPixmap> icons {
    {"array", {}},
    {"boolean", {}},
    {"class", {}},
    {"color", {}},
    {"constant", {}},
    {"enum-member", {}},
    {"enum", {}},
    {"event", {}},
    {"field", {}},
    {"function", {}},
    {"file", {}},
    {"interface", {}},
    {"key", {}},
    {"keyword", {}},
    {"method", {}},
    {"misc", {}},
    {"namespace", {}},
    {"numeric", {}},
    {"operator", {}},
    {"parameter", {}},
    {"property", {}},
    {"ruler", {}},
    {"snippet", {}},
    {"string", {}},
    {"structure", {}},
    {"variable", {}}
  };
  int sq_width = 0;
  QColor default_fg = Qt::blue;
  QColor default_bg = Qt::transparent;
};

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
  PopupMenu(const HLState* state, QWidget* parent = nullptr);
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

  void set_max_items(std::size_t new_max);
  void set_max_chars(std::size_t new_max);
  inline void set_border_width(std::size_t new_width)
  {
    border_width = new_width;
    update_dimensions();
  }
  inline void set_border_color(QColor new_color)
  {
    border_color = std::move(new_color);
  }
private:
  void update_dimensions();
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
  QColor border_color {0, 0, 0};
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
  PopupMenuIconManager icon_manager;
  bool has_scrollbar = false;
  bool is_hidden = true;
  QFont pmenu_font;
  float border_width = 1.f;
  Q_OBJECT
protected:
  void paintEvent(QPaintEvent* event) override;
};

#endif // NVUI_POPUPMENU_HPP
