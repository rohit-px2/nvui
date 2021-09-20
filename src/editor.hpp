#ifndef NVUI_EDITOR_HPP
#define NVUI_EDITOR_HPP

#include <cstdint>
#include <queue>
#include <span>
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
#include "object.hpp"

// For easily changing the type of 'char' in a cell
using grid_char = QString;

/// UI Capabilities (Extensions)
struct ExtensionCapabilities
{
  bool linegrid = false;
  bool popupmenu = false;
  bool wildmenu = false;
  bool messages = false;
  bool cmdline = false;
  bool multigrid = false;
};

/// Main editor area for Neovim
class EditorArea : public QWidget
{
  Q_OBJECT
public:
  using NeovimObj = Object;
  using u64 = std::uint64_t;
  using u32 = std::uint32_t;
  using u16 = std::uint16_t;
  EditorArea(
    QWidget* parent = nullptr,
    HLState* state = nullptr,
    Nvim* nv = nullptr
  );
  /**
   * Handles a Neovim "grid_resize" event.
   */
  void grid_resize(std::span<NeovimObj> objs);
  /**
   * Handles a Neovim "grid_line" event.
   */
  void grid_line(std::span<NeovimObj> objs);
  /**
   * Paints the grid cursor at the given grid, row, and column.
   */
  void grid_cursor_goto(std::span<NeovimObj> objs);
  /**
   * Handles a Neovim "option_set" event.
   */
  void option_set(std::span<NeovimObj> objs);
  /**
   * Handles a Neovim "flush" event.
   * This paints the internal buffer onto the window.
   */
  void flush();
  /**
   * Handles a Neovim "win_pos" event.
   */
  void win_pos(std::span<NeovimObj> objs);
  /**
   * Handles a Neovim "win_hide" event.
   */
  void win_hide(std::span<NeovimObj> objs);
  /**
   * Handles a Neovim "win_float_pos" event.
   */
  void win_float_pos(std::span<NeovimObj> objs);
  /**
   * Handles a Neovim "win_close" event.
   */
  void win_close(std::span<NeovimObj> objs);
  /**
   * Handles a Neovim "win_viewport" event.
   */
  void win_viewport(std::span<NeovimObj> objs);
  /// Handles a Neovim "grid_destroy" event.
  void grid_destroy(std::span<NeovimObj> objs);
  /// Handles a Neovim "msg_set_pos" event.
  void msg_set_pos(std::span<NeovimObj> objs);
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
  void grid_clear(std::span<NeovimObj> objs);
  /**
   * Handles a Neovim "grid_scroll" event
   */
  void grid_scroll(std::span<NeovimObj> objs);
  /**
   * Notify the editor area when resizing is enabled/disabled.
   */
  void set_resizing(bool is_resizing);
  /**
   * Handles a "mode_info_set" Neovim redraw event.
   * Internally sends the data to neovim_cursor.
   */
  void mode_info_set(std::span<NeovimObj> objs);
  /**
   * Handles a "mode_change" Neovim event.
   * Internally sends the data to neovim_cursor.
   */
  void mode_change(std::span<NeovimObj> objs);
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

  inline void popupmenu_show(std::span<NeovimObj> objs)
  {
    popup_menu.pum_show(objs);
  }

  inline void popupmenu_hide(std::span<NeovimObj> objs)
  {
    popup_menu.pum_hide(objs);
  }

  inline void popupmenu_select(std::span<NeovimObj> objs)
  {
    popup_menu.pum_sel(objs);
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

  inline void cmdline_show(std::span<NeovimObj> objs)
  {
    cmdline.cmdline_show(objs);
    popup_menu.attach_cmdline(cmdline.width());
  }
  inline void cmdline_hide(std::span<NeovimObj> objs)
  {
    cmdline.cmdline_hide(objs);
    popup_menu.detach_cmdline();
  }
  inline void cmdline_cursor_pos(std::span<NeovimObj> objs) { cmdline.cmdline_cursor_pos(objs); }
  inline void cmdline_special_char(std::span<NeovimObj> objs) { cmdline.cmdline_special_char(objs); }
  inline void cmdline_block_show(std::span<NeovimObj> objs) { cmdline.cmdline_block_show(objs); }
  inline void cmdline_block_append(std::span<NeovimObj> objs) { cmdline.cmdline_block_append(objs); }
  inline void cmdline_block_hide(std::span<NeovimObj> objs) { cmdline.cmdline_block_hide(objs); }
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
  /// Returns the highlight state of Neovim
  inline const HLState* hl_state() const { return state; }
  /// Returns the main font (first font in the fallback list).
  inline const QFont& main_font() const { return font; }
  /// Returns the font fallback list.
  inline const std::vector<Font>& fallback_list() const { return fonts; }
  /// Returns the offset needed to render the text properly using Qt's QPainter
  /// based on the current main font.
  double font_offset() const;
  /// Get Neovim's current default background color.
  /// If it has not been set, the color is black.
  QColor default_bg() const
  {
    return state->default_colors_get().bg().value_or(0).qcolor();
  }
  /// Get Neovim's default foreground color.
  /// If it has not been set, the color is white.
  QColor default_fg() const
  {
    return state->default_colors_get().fg().value_or(0x00ffffff).qcolor();
  }
  auto scroll_animation_duration() const { return scroll_animation_time; }
  auto move_animation_duration() const { return move_animation_time; }
  auto scroll_animation_frametime() const
  {
    return scroll_animation_frame_interval;
  }
  auto move_animation_frametime() const { return animation_frame_interval_ms; }
  bool animations_enabled() const { return animate; }
  void set_animations_enabled(bool enabled) { animate = enabled; }
  void set_animation_frametime(int ms)
  {
    if (ms >= 1) animation_frame_interval_ms = ms;
  }
  void set_move_animation_duration(float s)
  {
    if (s > 0.f) move_animation_time = s;
  }
  auto snapshot_limit() { return snapshot_count; }
  void set_move_scaler(std::string scaler)
  {
    if (scalers::scalers().contains(scaler))
    {
      GridBase::move_scaler = scalers::scalers().at(scaler);
    }
  }
  void set_scroll_scaler(std::string scaler)
  {
    if (scalers::scalers().contains(scaler))
    {
      GridBase::scroll_scaler = scalers::scalers().at(scaler);
    }
  }
  void set_cursor_scaler(std::string scaler)
  {
    if (scalers::scalers().contains(scaler))
    {
      Cursor::animation_scaler = scalers::scalers().at(scaler);
    }
  }
  void set_scroll_animation_duration(float dur)
  {
    if (dur > 0.f) scroll_animation_time = dur;
  }
  void set_scroll_frametime(int ms)
  {
    if (ms >= 1) scroll_animation_frame_interval = ms;
  }
  void set_snapshot_count(u32 count)
  {
    if (count > 0) snapshot_count = count;
  }
  std::vector<std::string> popupmenu_icon_list()
  {
    return popup_menu.icon_list();
  }
  void popupmenu_info_set_columns(int columns)
  {
    popup_menu.info_display().set_cols(columns);
  }
  float cursor_animation_duration() const
  {
    return cursor_animation_time;
  }
  int cursor_animation_frametime() const
  {
    return cursor_animation_frametime_ms;
  }
  void set_cursor_frametime(int ms)
  {
    cursor_animation_frametime_ms = ms;
  }
  void set_cursor_animation_duration(float secs)
  {
    cursor_animation_time = secs;
  }
  /// For input methods
  virtual QVariant inputMethodQuery(Qt::InputMethodQuery) const override;
protected:
  std::queue<PaintEventItem> events;
  QFontDatabase font_db;
  std::uint16_t charspace = 0;
  std::int16_t linespace = 0;
  HLState* state;
  bool should_ignore_pevent = false;
  std::vector<std::unique_ptr<GridBase>> grids;
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
  ExtensionCapabilities capabilities;
  bool animate = true;
  u32 snapshot_count = 4;
  float move_animation_time = 0.5f;
  int animation_frame_interval_ms = 10;
  int scroll_animation_frame_interval = 10;
  float scroll_animation_time = 0.3f;
  float cursor_animation_time = 0.3f;
  int cursor_animation_frametime_ms = 10;
  /**
   * Sets the current font to new_font.
   * If new_font is empty (this indicates an unset value),
   * uses a default font given by default_font_family() in utils.hpp.
   */
  void set_guifont(QString new_font);
  /**
   * Adds text to the given grid number at the given row and col number,
   * overwriting the previous text a the position.
   */
  void set_text(
    GridBase& g,
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
  GridBase* find_grid(const std::uint16_t grid_num);
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
  /// Creates a new grid with the given properties.
  /// NOTE: Override this if you want to create a grid with a different
  /// type (inheriting from GridBase). This is useful if you want to
  /// use different rendering technologies for grids. For example,
  /// WinEditorArea overrides this method and creates a
  /// D2DPaintGrid which uses Direct2D for drawing. If this method
  /// is not overriden, a QPaintGrid is created. You can also create
  /// a GridBase object if you have no need for the grids to do their
  /// own rendering.
  virtual void create_grid(u16 x, u16 y, u16 w, u16 h, u16 id)
  {
    grids.push_back(std::make_unique<QPaintGrid>(this, x, y, w, h, id));
  }
  /**
   * Draws a portion of the grid on the screen
   * (the area to draw is given by rect).
   */
  void draw_grid(
    QPainter& painter,
    const GridBase& grid,
    const QRect& rect
  );
  /**
   * Clears a portion of the grid by drawing Neovim's current default background
   color over it.
   * This should be faster than draw_grid if all you want to do is clear,
   * since it doesn't draw text/look for text to draw.
   */
  void clear_grid(QPainter& painter, const GridBase& grid, const QRect& rect);
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
  /**
   * Destroy the grid with the given grid_num.
   * If no grid exists with the given grid_num,
   * nothing happens.
   */
  void destroy_grid(std::uint16_t grid_num);
  /// Clear event queue
  inline void clear_events()
  {
    decltype(events)().swap(events);
  }
  /// Redraw all grids
  void send_redraw();
  /// Clear a grid
  void send_clear(std::uint16_t grid_num, QRect r = {});
  /// Draw a portion of the grid
  void send_draw(std::uint16_t grid_num, QRect r);
  /// Sorts grids in order of their z index.
  void sort_grids_by_z_index()
  {
    std::sort(grids.begin(), grids.end(), [](const auto& g1, const auto& g2) {
      return g1->winid < g2->winid;
    });
  }
  /// Returns an integer identifier for the window.
  u64 get_win(const NeovimExt& ext)
  {
    u64 x = 0;
    for(const auto& c : ext.data) x += static_cast<std::uint8_t>(c);
    return x;
  }
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
signals:
  void font_changed();
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
  void inputMethodEvent(QInputMethodEvent* event) override;
};

#endif // NVUI_EDITOR_HPP
