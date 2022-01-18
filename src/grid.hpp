#ifndef NVUI_GRID_HPP
#define NVUI_GRID_HPP

#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QString>
#include <cmath>
#include <queue>
#include <variant>
#include "scalers.hpp"

using grid_char = QString;

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
enum class PaintKind : std::uint8_t
{
  Clear,
  Draw,
  Redraw,
  Scroll
};

struct ScrollEventInfo
{
  QRect rect;
  // dx and dy are how many columns/rows to move right/down.
  // Positive values indicate right/down,
  // and negative values indicate left/up.
  int dx;
  int dy;
};

struct DrawEventInfo
{
  QRect rect;
};

struct RedrawEventInfo {};

struct ClearEventInfo
{
  QRect rect;
};

struct PaintEventItem
{
  bool is_scroll_event() const { return type == PaintKind::Scroll; }
  bool is_redraw_event() const { return type == PaintKind::Redraw; }
  bool is_clear_event() const { return type == PaintKind::Clear; }
  bool is_draw_event() const { return type == PaintKind::Draw; }
  const auto& scroll_info() const { return std::get<ScrollEventInfo>(event); }
  const auto& draw_info() const { return std::get<DrawEventInfo>(event); }
  const auto& redraw_info() const { return std::get<RedrawEventInfo>(event); }
  const auto& clear_info() const { return std::get<ClearEventInfo>(event); }
  PaintKind type;
  std::variant<
    ScrollEventInfo, DrawEventInfo, RedrawEventInfo, ClearEventInfo
  > event;
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
  /// For floating window ordering
  struct FloatOrderInfo
  {
    bool operator<(const FloatOrderInfo& other) const;
    int zindex;
    double x;
    double y;
  };
  using u16 = std::uint16_t;
  using u32 = std::uint32_t;
  using u64 = std::uint64_t;
  GridBase(
    double x,
    double y,
    u16 w,
    u16 h,
    u16 id
  );
  virtual ~GridBase() = default;
  void set_text(
    grid_char c,
    u16 row,
    u16 col,
    int hl_id,
    u16 repeat,
    bool is_dbl_width
  );
  /**
   * Set the size of the grid (cols x rows)
   * to width x height.
   */
  virtual void set_size(u16 w, u16 h);
  /**
   * Set the position of the grid in terms of
   * which row and column it starts on.
   */
  virtual void set_pos(double x, double y);
  void set_pos(QPoint p);
  /// Send a redraw message to the grid
  void send_redraw();
  void send_clear();
  void send_draw(QRect r);
  /// Grid's top left position
  QPoint top_left();
  QPoint bot_right();
  /// Grid's bottom right position
  QPoint bot_left();
  QPoint top_right();
  /// Clear the event queue
  void clear_event_queue();
  /// Change the viewport to the new viewport.
  virtual void viewport_changed(Viewport vp);
  bool is_float() const;
  void set_floating(bool f) noexcept;
  void win_pos(double x, double y);
  void float_pos(double x, double y);
  void msg_set_pos(double x, double y);
  void set_float_ordering_info(int zindex, const QPointF& p) noexcept;
  bool operator<(const GridBase& other) const noexcept;
  void scroll(int top, int bot, int left, int right, int rows);
  void clear();
public:
  double x;
  double y;
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
  FloatOrderInfo float_ordering_info;
  /// Not used in GridBase (may not even be used at all
  /// if animations are not supported/enabled by the rendering
  /// grid).
  static scalers::time_scaler scroll_scaler;
  static scalers::time_scaler move_scaler;
protected:
  // Flag that is set to "true" when the grid gets "modified"
  // To fix janky scrolling.
  // The problem is caused by Neovim sending 2 win_viewport events
  // One win_viewport event is sent before any grid modification is done
  // which causes the original grid snapshot to be drawn one line lower
  // than it should be
  // Then Neovim sends out the grid_line events but if the lag between
  // the two events is high enough there will be a delay and you will see
  // a frame of the old snapshot being drawn one line below which then
  // gets drawn over, and it looks bad.
  bool modified = false;
  bool is_msg_grid = false;
  // scroll_rect describes the original rectangle
  // rows is how many rows to scroll by in the up-direction
  // like how Neovim uses it in 'grid_scroll'
  // cols is how many columns to scroll by, although it's 0 for now
  struct ScrollArgs
  {
    QRect scroll_rect;
    // Where the scrolled region begins
    QPoint top_left;
    int rows;
    int cols;
  };
  static ScrollEventInfo convert_grid_scroll_args(
    int top, int bot, int left, int right, int rows, int cols = 0
  );
};

#endif // NVUI_GRID_HPP
