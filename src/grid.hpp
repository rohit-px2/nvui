#ifndef NVUI_GRID_HPP
#define NVUI_GRID_HPP

#include <QStaticText>
#include <QString>
#include <QTimer>
#include <QWidget>
#include <cmath>
#include <queue>
#include "hlstate.hpp"
#include "utils.hpp"
#include "scalers.hpp"
#include "cursor.hpp"

using grid_char = QString;

class EditorArea;

struct GridChar
{
  int hl_id; // Shouldn't have more than 65k highlight attributes
  grid_char text;
  bool double_width = false;
  std::uint32_t ucs;
  static grid_char grid_char_from_str(const std::string& s);
};

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

struct Viewport
{
  std::uint32_t topline;
  std::uint32_t botline;
  std::uint32_t curline;
  std::uint32_t curcol;
};

/// The base grid object, no rendering functionality.
/// Contains some convenience functions for setting text,
/// position, size, etc.
/// The event queue contains the rendering commands to
/// be performed.
class GridBase : public QObject
{
  Q_OBJECT
public:
  using u16 = std::uint16_t;
  using u32 = std::uint32_t;
  using u64 = std::uint64_t;
  GridBase(
    u16 x,
    u16 y,
    u16 w,
    u16 h,
    u16 id
  ) : x(x),
      y(y),
      cols(w),
      rows(h),
      id(id),
      area(w * h),
      viewport({0, 0, 0, 0})
  {
  }
  GridBase(const GridBase& other)
  : QObject{}, x(other.x), y(other.y), cols(other.cols), rows(other.rows),
    id(other.id), area(other.area), hidden(other.hidden),
    viewport(other.viewport)
  {
  }
  GridBase& operator=(const GridBase& other)
  {
    x = other.x;
    y = other.y;
    cols = other.cols;
    rows = other.rows;
    id = other.id;
    area = other.area;
    hidden = other.hidden;
    viewport = other.viewport;
    return *this;
  }
  virtual ~GridBase() = default;
  void set_text(
    grid_char c,
    u16 row,
    u16 col,
    int hl_id,
    u16 repeat,
    bool is_dbl_width
  )
  {
    std::uint32_t ucs;
    if (c.isEmpty()) ucs = 0;
    else
    {
      if (c.at(0).isHighSurrogate())
      {
        assert(c.size() >= 2);
        ucs = QChar::surrogateToUcs4(c.at(0), c.at(1));
      }
      else ucs = c.at(0).unicode();
    }
    // Neovim should make sure this isn't out-of-bounds
    assert(col + repeat <= cols);
    for(std::uint16_t i = 0; i < repeat; ++i)
    {
      std::size_t idx = row * cols + col + i;
      if (idx >= area.size()) return;
      area[idx] = {hl_id, c, is_dbl_width, ucs};
    }
  }

  /**
   * Set the size of the grid (cols x rows)
   * to width x height.
   */
  virtual void set_size(u16 w, u16 h)
  {
    static const GridChar empty_cell = {0, " ", false, QChar(' ').unicode()};
    resize_1d_vector(area, w, h, cols, rows, empty_cell);
    cols = w;
    rows = h;
  }
  /**
   * Set the position of the grid in terms of
   * which row and column it starts on.
   */
  virtual void set_pos(u16 new_x, u16 new_y)
  {
    x = new_x;
    y = new_y;
  }
  void set_pos(QPoint p) { set_pos(p.x(), p.y()); }
  /// Send a redraw message to the grid
  void send_redraw()
  {
    clear_event_queue();
    evt_q.push({PaintKind::Redraw, 0, QRect()});
  }
  void send_clear()
  {
    clear_event_queue();
    evt_q.push({PaintKind::Clear, 0, QRect()});
  }
  void send_draw(QRect r)
  {
    evt_q.push({PaintKind::Draw, 0, r});
  }
  /// Grid's top left position
  QPoint top_left() { return {x, y}; };
  QPoint bot_right() { return {x + cols, y + rows}; }
  /// Grid's bottom right position
  QPoint bot_left() { return {x, y + rows}; }
  QPoint top_right() { return { x + cols, y}; }
  /// Clear the event queue
  void clear_event_queue()
  {
    decltype(evt_q)().swap(evt_q);
  }
  /// Change the viewport to the new viewport.
  virtual void viewport_changed(Viewport vp)
  {
    viewport = vp;
  }
  bool is_float() const { return is_float_grid; }
  void set_floating(bool f) { is_float_grid = f; }
  void win_pos(u16 x, u16 y)
  {
    set_pos(x, y);
    set_floating(false);
  }
  void float_pos(u16 x, u16 y)
  {
    set_pos(x, y);
    set_floating(true);
  }
public:
  u16 x;
  u16 y;
  u16 cols;
  u16 rows;
  u16 id;
  std::size_t z_index = 0;
  std::int64_t winid = 0;
  std::vector<GridChar> area; // Size = rows * cols
  bool hidden = false;
  std::queue<PaintEventItem> evt_q;
  Viewport viewport;
  bool is_float_grid = false;
  /// Not used in GridBase (may not even be used at all
  /// if animations are not supported/enabled by the rendering
  /// grid).
  static scalers::time_scaler scroll_scaler;
  static scalers::time_scaler move_scaler;
};

#endif // NVUI_GRID_HPP
