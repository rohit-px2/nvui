#ifndef NVUI_POPUPMENU_HPP
#define NVUI_POPUPMENU_HPP
#include <optional>
#include <QWidget>
#include <QCompleter>
#include <QDebug>
#include <QPaintEvent>
#include <QStringBuilder>
#include <span>
#include <msgpack.hpp>
#include "hlstate.hpp"
#include "object.hpp"
#include "utils.hpp"
#include <fmt/core.h>
#include <fmt/format.h>
#include "constants.hpp"

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
  inline void set_default_fg(QColor fg)
  {
    default_fg = std::move(fg);
    load_icons(sq_width);
  }
  inline void set_default_bg(QColor bg)
  {
    default_bg = std::move(bg);
    load_icons(sq_width);
  }

  inline void set_bg_for_name(const QString& name, QColor bg)
  {
    fg_bg pair = {default_fg, default_bg};
    if (colors.contains(name)) pair = colors[name];
    pair.second = std::move(bg);
    colors[name] = pair;
    update_icon(name);
  }

  inline void set_fg_for_name(const QString& name, QColor fg)
  {
    fg_bg pair = {default_fg, default_bg};
    if (colors.contains(name)) pair = colors[name];
    pair.first = fg;
    colors[name] = pair;
    update_icon(name);
  }

  inline void set_fg_bg_for_name(const QString& name, QColor fg, QColor bg)
  {
    colors[name] = {std::move(fg), std::move(bg)};
    update_icon(name);
  }

  inline void update_icon(const QString& name)
  {
    if (!icons.contains(name)) return;
    else icons[name] = load_icon(name, sq_width);
  }

  std::vector<std::string> icon_list() const
  {
    std::vector<std::string> vs;
    const auto keys = icons.keys();
    for(const auto& icon_name : keys)
    {
      vs.push_back(icon_name.toStdString());
    }
    return vs;
  }

  const QColor* bg_for_kind(const QString& kind) const
  {
    if (!colors.contains(kind)) return nullptr;
    const auto& clr = colors[kind].second;
    if (!clr) return &default_bg;
    return &*clr;
  }
private:
  QPixmap load_icon(const QString& iname, int width);
  void load_icons(int width);
  QString iname_to_kind(const QString& iname);
  QString kind_to_iname(QString kind);
  // Map string (iname) to (foreground, background) tuple
  // Any color with no value will default to the default_foreground and default_background
  // colors.
  QHash<QString, fg_bg> colors {
    {"array", {}},
    {"boolean", {}},
    {"class", {}},
    {"color", {}},
    {"constant", {}},
    {"constructor", {}},
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
    {"module", {}},
    {"namespace", {}},
    {"numeric", {}},
    {"operator", {}},
    {"parameter", {}},
    {"property", {}},
    {"reference", {}},
    {"ruler", {}},
    {"snippet", {}},
    {"string", {}},
    {"structure", {}},
    {"variable", {}}
  };
  QHash<QString, QPixmap> icons {
    {"array", {}},
    {"boolean", {}},
    {"class", {}},
    {"color", {}},
    {"constant", {}},
    {"constructor", {}},
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
    {"module", {}},
    {"namespace", {}},
    {"numeric", {}},
    {"operator", {}},
    {"parameter", {}},
    {"property", {}},
    {"reference", {}},
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

class PopupMenu;
class PopupMenuInfo : public QWidget
{
  Q_OBJECT
public:
  PopupMenuInfo(PopupMenu* parent);
  void draw(QPainter& p, const HLAttr& attr, const QString& info);
  void set_cols(int c) { if (c > 0) cols = c; }
private:
  int cols = 50;
  PopupMenu* parent_menu = nullptr;
  HLAttr current_attr;
  QString current_text;
protected:
  void paintEvent(QPaintEvent* event) override;
};

class PopupMenu : public QWidget
{
  friend class PopupMenuInfo;
public:
  PopupMenu(const HLState* state, QWidget* parent = nullptr);
  /**
   * Handles a Neovim "popupmenu_show" event, showing the popupmenu with the
   * given items.
   */
  void pum_show(std::span<Object> objs);
  /**
   * Hides the popupmenu.
   */
  void pum_hide(std::span<Object> objs);
  /**
   * Handles a Neovim "popupmenu_select" event, selecting the
   * popupmenu item at the given index,
   * or selecting nothing if the index is -1.
   */
  void pum_sel(std::span<Object> objs);
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
      attached_width.value_or(max_chars * cell_width + border_width * 2),
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

  /**
   * Toggle the icons between being on and off.
   */
  inline void toggle_icons_enabled()
  {
    icons_enabled = !icons_enabled;
    paint();
  }

  /**
   * Set the size of the icons relative to the cell height.
   * The icons are normally in a square and by default they take
   * the entire cell height.
   * However if you want them to look smaller than the cell height,
   * you can call this with a negative offset.
   * If offset is positive, nothing will happen. This is so that
   * the icons don't clip other things.
   */
  inline void set_icon_size_offset(int offset)
  {
    if (offset > 0) return;
    else
    {
      icon_size_offset = offset;
      icon_manager.size_changed(cell_height + icon_size_offset);
    }
  }

  inline void set_icon_space(float space)
  {
    icon_space = space;
  }

  inline void set_icon_fg(const QString& icon_name, QColor fg)
  {
    icon_manager.set_fg_for_name(icon_name, std::move(fg));
  }

  inline void set_icon_bg(const QString& icon_name, QColor bg)
  {
    icon_manager.set_bg_for_name(icon_name, std::move(bg));
  }

  inline void set_icon_colors(const QString& icon_name, QColor fg, QColor bg)
  {
    icon_manager.set_fg_bg_for_name(icon_name, std::move(fg), std::move(bg));
  }

  inline void set_default_icon_fg(QColor fg)
  {
    icon_manager.set_default_fg(std::move(fg));
  }

  inline void set_default_icon_bg(QColor bg)
  {
    icon_manager.set_default_bg(std::move(bg));
  }

  inline void attach_cmdline(int width)
  {
    attached_width = width;
    update_dimensions();
    paint();
  }

  inline void cmdline_width_changed(int width)
  {
    if (!attached_width) return;
    else attach_cmdline(width);
  }

  inline void detach_cmdline()
  {
    attached_width.reset();
    update_dimensions();
  }

  inline void set_icons_on_right(bool right_icons)
  {
    icons_on_right = right_icons;
    paint();
  }

  inline auto icon_list() const
  {
    return icon_manager.icon_list();
  }

  auto& info_display() { return info_widget; }

  auto selected_idx() { return cur_selected; }
private:
  void update_dimensions();
  /**
   * Add the given popupmenu items to the popup menu.
   */
  void add_items(ObjectArray& items);
  /**
   * Redraw the popupmenu.
   */
  void paint();
  /**
   * Draw the given popup menu item starting at the given y-coordinate
   * and with the given attribute.
   */
  void draw_with_attr(QPainter& p, const HLAttr& attr, const PMenuItem& item, int y);
  /**
   * Draw the info as its own box.
   */
  void draw_info(QPainter& p, const HLAttr& attr, const QString& info);
  std::optional<int> attached_width;
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
  PopupMenuInfo info_widget;
  PopupMenuIconManager icon_manager;
  bool has_scrollbar = false;
  bool is_hidden = true;
  bool icons_enabled = true;
  bool icons_on_right = false;
  float icon_space = 1.5f;
  int icon_size_offset = 0;
  QFont pmenu_font;
  float border_width = 1.f;
  Q_OBJECT
protected:
  void paintEvent(QPaintEvent* event) override;
};

#endif // NVUI_POPUPMENU_HPP
