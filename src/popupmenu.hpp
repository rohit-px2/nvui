#ifndef NVUI_POPUPMENU_HPP
#define NVUI_POPUPMENU_HPP
#include <optional>
#include <QWidget>
#include <QCompleter>
#include <QDebug>
#include <QPaintEvent>
#include <QStaticText>
#include <QStringBuilder>
#include <span>
#include <msgpack.hpp>
#include "hlstate.hpp"
#include "object.hpp"
#include "utils.hpp"
#include <fmt/core.h>
#include <fmt/format.h>
#include "constants.hpp"
#include "font.hpp"

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

  int icon_size() const { return sq_width; }
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

class Nvim;
struct Cmdline;

class PopupMenu
{
public:
  PopupMenu(const HLState* state);
  virtual ~PopupMenu() = default;
  virtual void register_nvim(Nvim& nvim) = 0;
  virtual QRect get_rect() const = 0;
  /**
   * Handles a Neovim "popupmenu_show" event, showing the popupmenu with the
   * given items.
   */
  void pum_show(std::span<const Object> objs);

  void pum_show(
    const ObjectArray& items,
    int selected,
    int grid_num,
    int row,
    int col,
    FontDimensions dims,
    int grid_x = -1,
    int grid_y = -1
  );
  /**
   * Hides the popupmenu.
   */
  void pum_hide(std::span<const Object> objs);
  /**
   * Handles a Neovim "popupmenu_select" event, selecting the
   * popupmenu item at the given index,
   * or selecting nothing if the index is -1.
   */
  void pum_sel(std::span<const Object> objs);
  /**
   * Update the highlight attributes used for drawing the popup menu.
   */
  void update_highlight_attributes();
  inline bool hidden() { return is_hidden; }
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

  inline void set_border_width(std::size_t new_width)
  {
    border_width = new_width;
    update_dimensions();
  }

  inline void set_border_color(Color new_color)
  {
    border_color = new_color;
  }

  void attach_cmdline(Cmdline*);

  inline void attach_cmdline(int width)
  {
    attached_width = width;
    update_dimensions();
    redraw();
  }

  inline void cmdline_width_changed(int width)
  {
    if (!attached_width) return;
    else attach_cmdline(width);
  }

  void detach_cmdline();

  auto selected_idx() { return cur_selected; }

  struct Rectangle
  {
    int x;
    int y;
    int w;
    int h;
  };
  virtual Rectangle dimensions_for(
    int x, int y, int screenwidth, int screenheight
  ) = 0;
protected:
  virtual void do_show() = 0;
  virtual void do_hide() = 0;
  virtual void update_dimensions() = 0;
  QRect calc_rect(int width, int height, int max_x, int max_y) const;
  /**
   * Add the given popupmenu items to the popup menu.
   */
  void add_items(const ObjectArray& items);
  /**
   * Redraw the popupmenu.
   */
  virtual void redraw() = 0;
  // Optional attached command line.
  // When this is not nullptr,
  // indicates the popup menu should be attached to
  // the command line.
  Cmdline* cmdline = nullptr;
  FontDimensions parent_dims;
  std::optional<int> attached_width;
  int pixel_x = -1;
  int pixel_y = -1;
  const HLState* hl_state;
  const HLAttr* pmenu = nullptr;
  const HLAttr* pmenu_sel = nullptr;
  const HLAttr* pmenu_sbar = nullptr;
  const HLAttr* pmenu_thumb = nullptr;
  Color border_color {0, 0, 0};
  int cur_selected = -1;
  float font_ascent = 0.f;
  std::vector<PMenuItem> completion_items;
  int grid_num = 0;
  int row = 0;
  int col = 0;
  int grid_x = 0;
  int grid_y = 0;
  int linespace = 0;
  bool is_hidden = true;
  float border_width = 1.f;
  int longest_word_size = 0;
};

class PopupMenuQ : public PopupMenu, public QWidget
{
public:
  PopupMenuQ(const HLState* state, QWidget* parent = nullptr);
  ~PopupMenuQ() override;
  QRect get_rect() const override;
  void font_changed(const QFont& font, FontDimensions dims);
  /**
   * Toggle the icons between being on and off.
   */
  void register_nvim(Nvim&) override;
  inline void toggle_icons_enabled()
  {
    icons_enabled = !icons_enabled;
    redraw();
  }
  inline auto icon_list() const
  {
    return icon_manager.icon_list();
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
  void set_icon_size_offset(int offset);

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
  void draw_with_attr(
    QPainter& p, const HLAttr& attr,
    const PMenuItem& item, int y
  );
  Rectangle dimensions_for(int x, int y, int w, int h) override;
protected:
  void paintEvent(QPaintEvent*) override;
private:
  std::size_t max_possible_items() const;
  int item_height() const;
  int max_items() const;
  void do_hide() override;
  void do_show() override;
  void redraw() override;
  void update_dimensions() override;
  void paint();
  int max_chars = 0;
  QPixmap pixmap;
  PopupMenuIconManager icon_manager;
  bool icons_enabled = true;
  float icon_space = 1.1f;
  int icon_size_offset = 0;
  QFont pmenu_font;
  FontDimensions dimensions {1, 1};
  QHash<QString, QStaticText> text_cache;
};

#endif // NVUI_POPUPMENU_HPP
