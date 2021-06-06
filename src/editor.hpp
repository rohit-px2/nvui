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

// For easily changing the type of 'char' in a cell
using grid_char = QString;

struct GridChar
{
  std::uint16_t hl_id; // Shouldn't have more than 65k highlight attributes
  grid_char text;
  bool double_width = false;
};

struct Grid
{
  std::uint16_t x;
  std::uint16_t y;
  std::uint16_t rows;
  std::uint16_t cols;
  std::uint16_t id;
  std::vector<GridChar> area; // Size = rows * cols
  bool hidden = false;
};

/// Main editor area for Neovim
class EditorArea : public QWidget
{
  Q_OBJECT
public:
  EditorArea(
    QWidget* parent = nullptr,
    HLState* state = nullptr,
    Nvim* nv = nullptr
  );
  /**
   * Handles a Neovim "grid_resize" event.
   */
  void grid_resize(const msgpack::object* obj, std::uint32_t size);
  /**
   * Handles a Neovim "grid_line" event.
   */
  void grid_line(const msgpack::object* obj, std::uint32_t size);
  /**
   * Paints the grid cursor at the given grid, row, and column.
   */
  void grid_cursor_goto(const msgpack::object* obj, std::uint32_t size);
  /**
   * Handles a Neovim "option_set" event.
   */
  void option_set(const msgpack::object* obj, std::uint32_t size);
  /**
   * Handles a Neovim "flush" event.
   * This paints the internal buffer onto the window.
   */
  void flush();
  /**
   * Handles a Neovim "win_pos" event.
   */
  void win_pos(const msgpack::object* obj);
  /**
   * Returns the font width and font height.
   */
  std::tuple<std::uint16_t, std::uint16_t> font_dimensions() const;
  /**
   * Ignores the next paintEvent call.
   * This is really only called after the window is moved.
   */
  void ignore_next_paint_event();
  /**
   * Handles a Neovim "grid_clear" event
   */
  void grid_clear(const msgpack::object* obj, std::uint32_t size);
  /**
   * Handles a Neovim "grid_scroll" event
   */
  void grid_scroll(const msgpack::object* obj, std::uint32_t size);
  /**
   * Notify the editor area when resizing is enabled/disabled.
   */
  void set_resizing(bool is_resizing);
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
  std::vector<QFont> fonts;
  std::uint16_t font_width;
  std::uint16_t font_height;
  QFont font;
  Nvim* nvim;
  QPixmap pixmap;
  bool resizing = false;
  int rows = -1;
  int cols = -1;
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
  QSize to_rc(const QSize& pixel_size);
  /**
   * Updates the font metrics, such as font_width and font_height.
   */
  void update_font_metrics();
  /**
   * Draws a portion of the grid on the screen
   * (the area to draw is given by rect).
   */
  void draw_grid(QPainter& painter, const Grid& grid, const QRect& rect);
  /**
   * Clears a portion of the grid by drawing Neovim's current default background
   color over it.
   * This should be faster than draw_grid if all you want to do is clear,
   * since it doesn't draw text/look for text to draw.
   */
  void clear_grid(QPainter& painter, const Grid& grid, const QRect& rect);
public slots:
  /**
   * Handle a window resize.
   */
  void resized(QSize size);
protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  bool focusNextPrevChild(bool next) override;
};

#endif // NVUI_EDITOR_HPP
