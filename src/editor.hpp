#ifndef NVUI_EDITOR_HPP
#define NVUI_EDITOR_HPP

#include <cstdint>
#include <queue>
#include <msgpack.hpp>
#include <QFont>
#include <QObject>
#include <QRect>
#include <QWidget>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QFontDatabase>
#include "hlstate.hpp"
#include "nvim.hpp"
#include "cursor.hpp"
#include "popupmenu.hpp"
#include "cmdline.hpp"
#include "font.hpp"
#include "grid.hpp"

// For easily changing the type of 'char' in a cell
using grid_char = QString;

/// Main editor area for Neovim
class EditorArea : public QWidget
{
  Q_OBJECT
public:
  using NeovimObj = const msgpack::object*;
  using msg_size = std::uint32_t;
  EditorArea(
    QWidget* parent = nullptr,
    HLState* state = nullptr,
    Nvim* nv = nullptr
  );
  /**
   * Handles a Neovim "grid_resize" event.
   */
  void grid_resize(NeovimObj obj, msg_size size);
  /**
   * Handles a Neovim "grid_line" event.
   */
  void grid_line(NeovimObj obj, msg_size size);
  /**
   * Paints the grid cursor at the given grid, row, and column.
   */
  void grid_cursor_goto(NeovimObj obj, msg_size size);
  /**
   * Handles a Neovim "option_set" event.
   */
  void option_set(NeovimObj obj, msg_size size);
  /**
   * Handles a Neovim "flush" event.
   * This paints the internal buffer onto the window.
   */
  void flush();
  /**
   * Handles a Neovim "win_pos" event.
   */
  void win_pos(NeovimObj obj);
  /**
   * Returns the font width and font height.
   */
  virtual std::tuple<float, float> font_dimensions() const;
  /**
   * Ignores the next paintEvent call.
   * This is really only called after the window is moved.
   */
  void ignore_next_paint_event();
  /**
   * Handles a Neovim "grid_clear" event
   */
  void grid_clear(NeovimObj obj, msg_size size);
  /**
   * Handles a Neovim "grid_scroll" event
   */
  void grid_scroll(NeovimObj obj, msg_size size);
  /**
   * Notify the editor area when resizing is enabled/disabled.
   */
  void set_resizing(bool is_resizing);
  /**
   * Handles a "mode_info_set" Neovim redraw event.
   * Internally sends the data to neovim_cursor.
   */
  void mode_info_set(NeovimObj obj, msg_size size);
  /**
   * Handles a "mode_change" Neovim event.
   * Internally sends the data to neovim_cursor.
   */
  void mode_change(NeovimObj obj, msg_size size);
  /**
   * Handles a "busy_start" event, passing it to the Neovim cursor
   */
  inline void busy_start()
  {
    neovim_cursor.busy_start();
  }
  /**
   * Handles a "busy_stop" event, passing it to the Neovim cursor
   */
  inline void busy_stop()
  {
    neovim_cursor.busy_stop();
  }
  /**
   * Set the character spacing (space between individual characters)
   to space.
   */
  inline void set_charspace(std::uint16_t space)
  {
    charspace = space;
    update_font_metrics();
    resized(size());
  }

  inline void set_caret_dimensions(float extend_top, float extend_bottom)
  {
    neovim_cursor.set_caret_extend(extend_top, extend_bottom);
  }
  inline void set_caret_top(float top) { neovim_cursor.set_caret_extend_top(top); }
  inline void set_caret_bottom(float bot) { neovim_cursor.set_caret_extend_bottom(bot); }

  inline void popupmenu_show(NeovimObj obj, msg_size size)
  {
    popup_menu.pum_show(obj, size);
  }

  inline void popupmenu_hide(NeovimObj obj, msg_size size)
  {
    popup_menu.pum_hide(obj, size);
  }

  inline void popupmenu_select(NeovimObj obj, msg_size size)
  {
    popup_menu.pum_sel(obj, size);
  }

  inline void popupmenu_icons_toggle()
  {
    popup_menu.toggle_icons_enabled();
  }

  inline void popupmenu_set_icon_size_offset(int offset)
  {
    popup_menu.set_icon_size_offset(offset);
  }

  inline void popupmenu_set_icon_spacing(float spacing)
  {
    popup_menu.set_icon_space(spacing);
  }

  inline void popupmenu_set_max_chars(std::size_t max) { popup_menu.set_max_chars(max); }
  inline void popupmenu_set_max_items(std::size_t max) { popup_menu.set_max_items(max); }
  inline void popupmenu_set_border_width(std::size_t width) { popup_menu.set_border_width(width); }
  inline void popupmenu_set_border_color(QColor new_color)
  {
    popup_menu.set_border_color(new_color);
  }

  inline void popupmenu_set_icon_fg(const QString& icon_name, QColor color)
  {
    popup_menu.set_icon_fg(icon_name, std::move(color));
  }

  inline void popupmenu_set_icon_bg(const QString& icon_name, QColor color)
  {
    popup_menu.set_icon_bg(icon_name, std::move(color));
  }

  inline void popupmenu_set_icon_colors(const QString& icon_name, QColor fg, QColor bg)
  {
    popup_menu.set_icon_colors(icon_name, std::move(fg), std::move(bg));
  }

  inline void popupmenu_set_default_icon_bg(QColor bg)
  {
    popup_menu.set_default_icon_bg(std::move(bg));
  }

  inline void popupmenu_set_default_icon_fg(QColor fg)
  {
    popup_menu.set_default_icon_fg(std::move(fg));
  }

  inline void popupmenu_set_icons_right(bool right) { popup_menu.set_icons_on_right(right); }

  inline void cmdline_show(NeovimObj obj, msg_size size)
  {
    cmdline.cmdline_show(obj, size);
    popup_menu.attach_cmdline(cmdline.width());
  }
  inline void cmdline_hide(NeovimObj obj, msg_size size)
  {
    cmdline.cmdline_hide(obj, size);
    popup_menu.detach_cmdline();
  }
  inline void cmdline_cursor_pos(NeovimObj obj, msg_size size) { cmdline.cmdline_cursor_pos(obj, size); }
  inline void cmdline_special_char(NeovimObj obj, msg_size size) { cmdline.cmdline_special_char(obj, size); }
  inline void cmdline_block_show(NeovimObj obj, msg_size size) { cmdline.cmdline_block_show(obj, size); }
  inline void cmdline_block_append(NeovimObj obj, msg_size size) { cmdline.cmdline_block_append(obj, size); }
  inline void cmdline_block_hide(NeovimObj obj, msg_size size) { cmdline.cmdline_block_hide(obj, size); }
  inline void reposition_cmdline()
  {
    QPointF rel_pos = cmdline.relative_pos();
    cmdline.move(rel_pos.x() * size().width(), rel_pos.y() * size().height());
  }
  inline void cmdline_set_font_size(float size) { cmdline.set_font_size(size); }
  inline void cmdline_set_font_family(const QString& family) { cmdline.set_font_family(family); }
  inline void cmdline_set_fg(QColor fg) { cmdline.set_fg(fg); }
  inline void cmdline_set_bg(QColor bg) { cmdline.set_bg(bg); }
  inline void cmdline_set_border_width(int width) { cmdline.set_border_width(width); }
  inline void cmdline_set_border_color(QColor color) { cmdline.set_border_color(color); }
  inline void cmdline_set_font_scale_ratio(float ratio) { cmdline.set_big_font_scale_ratio(ratio); }
  inline void cmdline_set_x(float x) { cmdline.set_x(x); reposition_cmdline(); }
  inline void cmdline_set_y(float y) { cmdline.set_y(y); reposition_cmdline(); }
  inline void cmdline_set_width(float width) { cmdline.set_width(width); reposition_cmdline(); }
  inline void cmdline_set_height(float height) { cmdline.set_height(height); reposition_cmdline(); }
  inline void cmdline_set_center_x(float x) { cmdline.set_center_x(x); reposition_cmdline(); }
  inline void cmdline_set_center_y(float y) { cmdline.set_center_y(y); reposition_cmdline(); }
  inline void cmdline_set_padding(int padding) { cmdline.set_padding(padding); }
  inline void set_mouse_enabled(bool enabled) { mouse_enabled = enabled; }
protected:
  // Differentiate between redrawing and clearing (since clearing is
  // a lot easier)
  enum PaintKind : std::uint8_t
  {
    Clear,
    Draw,
    Redraw
  };
  struct PaintEventItem
  {
    PaintKind type;
    std::uint16_t grid_num;
    QRect rect;
  };
  std::queue<PaintEventItem> events;
  QFontDatabase font_db;
  std::uint16_t charspace = 0;
  std::int16_t linespace = 0;
  HLState* state;
  bool should_ignore_pevent = false;
  std::vector<Grid> grids;
  bool bold = false;
  // For font fallback, not used if a single font is set.
  std::vector<Font> fonts;
  std::uint16_t font_width;
  std::uint16_t font_height;
  QFont font;
  Nvim* nvim;
  QPixmap pixmap;
  bool resizing = false;
  Cursor neovim_cursor;
  int rows = -1;
  int cols = -1;
  PopupMenu popup_menu;
  CmdLine cmdline;
  bool neovim_is_resizing = false;
  std::optional<QSize> queued_resize = std::nullopt;
  std::unordered_map<std::uint32_t, std::uint32_t> font_for_unicode;
  bool mouse_enabled = false;
  /**
   * Sets the current font to new_font.
   */
  void set_guifont(const QString& new_font);
  /**
   * Adds text to the given grid number at the given row and col number,
   * overwriting the previous text a the position.
   */
  void set_text(
    Grid& g,
    grid_char c,
    std::uint16_t row,
    std::uint16_t col,
    std::uint16_t hl_id,
    std::uint16_t repeat = 1,
    bool is_dbl_width = false
  );
  /**
   * Returns a grid with the matching grid_num
   */
  Grid* find_grid(const std::uint16_t grid_num);
  /**
   * Converts a rectangle in terms of rows and cols
   * to a pixel-value rectangle relative to the top-left
   * corner of the editor area.
   */
  QRect to_pixels(
    const std::uint16_t x,
    const std::uint16_t y,
    const std::uint16_t width,
    const std::uint16_t height
  );
  /**
   * Converts a QSize from pixel size to rows and columns
   * based on the current font size.
   */
  virtual QSize to_rc(const QSize& pixel_size);
  /**
   * Updates the font metrics, such as font_width and font_height.
   */
  virtual void update_font_metrics(bool update_fonts = false);
  /**
   * Draws a portion of the grid on the screen
   * (the area to draw is given by rect).
   */
  void draw_grid(
    QPainter& painter,
    const Grid& grid,
    const QRect& rect,
    std::unordered_set<int>& drawn_rows
  );
  /**
   * Clears a portion of the grid by drawing Neovim's current default background
   color over it.
   * This should be faster than draw_grid if all you want to do is clear,
   * since it doesn't draw text/look for text to draw.
   */
  void clear_grid(QPainter& painter, const Grid& grid, const QRect& rect);
  /**
   * Draws the text with the background and foreground according to the
   * given attributes attr and the default colors, between start and end.
   */
  void draw_text_and_bg(
    QPainter& painter,
    const QString& text,
    const HLAttr& attr,
    const HLAttr& def_clrs,
    const QPointF& start,
    const QPointF& end,
    const int offset,
    QFont font
  );
  /**
   * Draw the cursor
   */
  void draw_cursor(QPainter& painter);
  /**
   * Draw the popup menu.
   * NOTE: This function does not check if the popup menu
   * is hidden before drawing it.
   */
  void draw_popup_menu();
  /**
   * Hide the popup menu.
   */
  inline void hide_popup_menu()
  {
    popup_menu.hide();
  }

  struct GridPos
  {
    int grid_num;
    int row;
    int col;
  };
  /**
   * Get the grid num, row, and column for the given
   * (x, y) pixel position.
   * Returns nullopt if no grid could be found that
   * matches the requirements.
   */
  std::optional<GridPos> grid_pos_for(const QPoint& pos);
  /**
   * Calculate the position for the mouse click event
   * and send the mouse input with the given button,
   * action, and modifiers.
   */
  void send_mouse_input(
    QPoint pos,
    std::string button,
    std::string action,
    std::string modifiers
  );
public slots:
  /**
   * Handle a window resize.
   */
  void resized(QSize size);
  /**
   * Activated when Neovim's default colors change.
   * We need to redraw the entire area, since Neovim doesn't send the grid
   * lines for us.
   */
  virtual void default_colors_changed(QColor fg, QColor bg);
  void set_fallback_for_ucs(std::uint32_t ucs);
  std::uint32_t font_for_ucs(std::uint32_t ucs);
protected:
  void resizeEvent(QResizeEvent* event) override;
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  bool focusNextPrevChild(bool next) override;
  void dropEvent(QDropEvent* event) override;
  void dragEnterEvent(QDragEnterEvent* event) override;
};

#endif // NVUI_EDITOR_HPP
