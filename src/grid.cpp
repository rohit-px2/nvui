#include "grid.hpp"
#include "utils.hpp"

scalers::time_scaler GridBase::scroll_scaler = scalers::oneminusexpo2negative10;
scalers::time_scaler GridBase::move_scaler = scalers::oneminusexpo2negative10;

bool GridBase::FloatOrderInfo::operator<(const FloatOrderInfo& other) const
{
  return zindex == other.zindex
    ? x == other.x
      ? y == other.y
        ? true // Rip
        : y < other.y
      : x < other.x
    : zindex < other.zindex;
}

ScrollEventInfo GridBase::convert_grid_scroll_args(
  int top,
  int bot,
  int left,
  int right,
  int rows,
  int cols
)
{
  // Positive means upward,
  // Negative is downward.
  // dy is negative when rows is positive,
  // and positive when rows is negative
  // Don't know about cols right now but it's not being used
  if (rows > 0)
  {
    return {
      QRect(QPoint(left, top + rows), QPoint(right, bot)),
      -cols,
      -rows,
    };
  }
  else
  {
    return {
      QRect(QPoint(left, top), QPoint(right, bot + rows)),
      -cols,
      -rows,
    };
  }
}

GridBase::GridBase(
  double x,
  double y,
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

bool GridBase::operator<(const GridBase& other) const noexcept
{
  if (is_msg_grid) return false;
  else if (other.is_msg_grid) return true;
  else if (!is_float() && other.is_float())
  {
    return true;
  }
  else if (is_float() && !other.is_float())
  {
    return false;
  }
  else if (is_float() && other.is_float())
  {
    return float_ordering_info < other.float_ordering_info;
  }
  else
  {
    return id < other.id;
  }
}

grid_char GridChar::grid_char_from_str(const std::string& s)
{
  return QString::fromStdString(s);
}

void GridBase::scroll(int top, int bot, int left, int right, int rows)
{
  if (rows > 0)
  {
    // Original region is
    // (top + rows) .. bot (exclusive)
    // Scrolled region is
    // top .. bot - rows
    for(int y = top; y < (bot - rows); ++y)
    {
      for(int x = left; x < right && x < cols; ++x)
      {
        area[y * cols + x] = std::move(area[(y + rows) * cols + x]);
      }
    }
  }
  else if (rows < 0)
  {
    // Original region is
    // top .. bot + rows (exclusive)
    // Scrolled region is
    // top - rows .. bot
    for(int y = (bot-1); y >= (top - rows); --y)
    {
      for(int x = left; x <= right && x < cols; ++x)
      {
        area[y * cols + x] = std::move(area[(y + rows) * cols + x]);
      }
    }
  }
  evt_q.push({PaintKind::Scroll, convert_grid_scroll_args(top, bot, left, right, rows)});
  modified = true;
}

void GridBase::set_text(
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
  modified = true;
}
/**
 * Set the size of the grid (cols x rows)
 * to width x height.
 */
void GridBase::set_size(u16 w, u16 h)
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
void GridBase::set_pos(double new_x, double new_y)
{
  x = new_x;
  y = new_y;
}
void GridBase::msg_set_pos(double x, double y)
{
  is_msg_grid = true;
  set_pos(x, y);
}
void GridBase::set_pos(QPoint p) { set_pos(p.x(), p.y()); }
/// Send a redraw message to the grid
void GridBase::send_redraw()
{
  clear_event_queue();
  evt_q.push({PaintKind::Redraw, RedrawEventInfo {}});
}
void GridBase::send_clear()
{
  clear_event_queue();
  evt_q.push({PaintKind::Clear, ClearEventInfo {}});
}
void GridBase::send_draw(QRect r)
{
  evt_q.push({PaintKind::Draw, DrawEventInfo {r}});
}
/// Grid's top left position
QPoint GridBase::top_left() { return QPoint(x, y); };
QPoint GridBase::bot_right() { return QPoint(x + cols, y + rows); }
/// Grid's bottom right position
QPoint GridBase::bot_left() { return QPoint(x, y + rows); }
QPoint GridBase::top_right() { return QPoint( x + cols, y); }
/// Clear the event queue
void GridBase::clear_event_queue()
{
  decltype(evt_q)().swap(evt_q);
}
/// Change the viewport to the new viewport.
void GridBase::viewport_changed(Viewport vp)
{
  viewport = vp;
}
bool GridBase::is_float() const { return is_float_grid; }
void GridBase::set_floating(bool f) noexcept { is_float_grid = f; }

void GridBase::win_pos(double x, double y)
{
  set_floating(false);
  set_pos(x, y);
}

void GridBase::float_pos(double x, double y)
{
  set_floating(true);
  set_pos(x, y);
}

void GridBase::set_float_ordering_info(int zindex, const QPointF& p) noexcept
{
  float_ordering_info = {zindex, p.x(), p.y()};
}

void GridBase::clear()
{
  for(auto& gc : area)
  {
    gc = {0, " ", false, QChar(' ').unicode()};
  }
  send_clear();
}
