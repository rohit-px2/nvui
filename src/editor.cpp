#include "editor.hpp"
#include "msgpack_overrides.hpp"
#include "utils.hpp"
#include <chrono>
#include <limits>
#include <QApplication>
#include <QDebug>
#include <QDesktopWidget>
#include <QPainter>
#include <QScreen>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <locale>
#include <QSizePolicy>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <fmt/core.h>
#include <fmt/format.h>

static QColor to_qcolor(const Color& clr)
{
  return {clr.r, clr.g, clr.b};
}

static int get_offset(const QFont& font, const int linespacing)
{
  QFontMetrics fm {font};
  return fm.ascent() + (linespacing / 2);
}

EditorArea::EditorArea(QWidget* parent, HLState* hl_state, Nvim* nv)
: QWidget(parent),
  state(hl_state),
  nvim(nv),
  pixmap(QDesktopWidget().size()),
  neovim_cursor(),
  popup_menu(hl_state)
{
  setAttribute(Qt::WA_OpaquePaintEvent);
  setAutoFillBackground(false);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setFocusPolicy(Qt::StrongFocus);
  setFocus();
  setMouseTracking(true);
  font.setPixelSize(15);
  update_font_metrics();
  QObject::connect(&neovim_cursor, &Cursor::cursor_hidden, this, [this] {
    update();
  });
  QObject::connect(&neovim_cursor, &Cursor::cursor_visible, this, [this] {
    update();
  });
}

void EditorArea::set_text(
  Grid& grid,
  grid_char c,
  std::uint16_t row,
  std::uint16_t col,
  std::uint16_t hl_id,
  std::uint16_t repeat,
  bool is_dbl_width
)
{
  //std::cout << "Set " << repeat << " texts at (" << row << ", " << col << ").\n";
  // Neovim should make sure this isn't out-of-bounds
  assert(col + repeat <= grid.cols);
  for(std::uint16_t i = 0; i < repeat; ++i)
  {
    // row * grid.cols - get current row
    assert(row * grid.cols + col + i < grid.area.size());
    grid.area[row * grid.cols + col + i] = GridChar {hl_id, c, is_dbl_width};
  }
}

void EditorArea::grid_resize(const msgpack::object *obj, std::uint32_t size)
{
  // Should only run once
  for(std::uint32_t i = 0; i < size; ++i)
  {
    const msgpack::object& o = obj[i];
    assert(o.type == msgpack::type::ARRAY);
    const auto& arr = o.via.array;
    assert(arr.size == 3);
    std::uint16_t grid_num = arr.ptr[0].as<std::uint16_t>();
    //std::cout << "Resize grid " << grid_num << "\n";
    assert(grid_num != 0);
    std::uint16_t width = arr.ptr[1].as<std::uint16_t>();
    std::uint16_t height = arr.ptr[2].as<std::uint16_t>();
    Grid* grid = find_grid(grid_num);
    if (grid)
    {
      grid->rows = height;
      grid->cols = width;
      grid->area.resize(grid->rows * grid->cols);
    }
    else
    {
      grids.push_back(Grid {0, 0, height, width, grid_num, std::vector<GridChar>(width * height)});
    }
  }
}

void EditorArea::grid_line(const msgpack::object* obj, std::uint32_t size)
{
  //std::stringstream ss;
  std::uint16_t hl_id = 0;
  //std::cout << "Received grid line.\nNum params: " << size << '\n';
  for(std::uint32_t i = 0; i < size; ++i)
  {
    const msgpack::object& grid_cmd = obj[i];
    assert(grid_cmd.type == msgpack::type::ARRAY);
    const auto& grid = grid_cmd.via.array;
    assert(grid.size == 4);
    const std::uint16_t grid_num = grid.ptr[0].as<std::uint16_t>();
    //std::cout << "Grid line on grid " << grid_num << '\n';
    // Get associated grid
    Grid* grid_ptr = find_grid(grid_num);
    assert(grid_ptr);
    Grid& g = *grid_ptr;
    const std::uint16_t start_row = grid.ptr[1].as<std::uint16_t>();
    const std::uint16_t start_col = grid.ptr[2].as<std::uint16_t>();
    int col = start_col;
    const msgpack::object& cells_obj = grid.ptr[3];
    assert(cells_obj.type == msgpack::type::ARRAY);
    const auto& cells = cells_obj.via.array;
    for(std::uint32_t j = 0; j < cells.size; ++j)
    {
      // [text, (hl_id, repeat)]
      const msgpack::object& o = cells.ptr[j];
      assert(o.type == msgpack::type::ARRAY);
      const auto& seq = o.via.array;
      assert(seq.size >= 1 && seq.size <= 3);
      int repeat = 1;
      bool is_dbl = false;
      assert(seq.ptr[0].type == msgpack::type::STR);
      grid_char text = seq.ptr[0].as<decltype(text)>();
      //ss << text.size() << ' ';
      switch(seq.size)
      {
        case 1:
        {
          break;
        }
        case 2:
        {
          hl_id = seq.ptr[1].as<std::uint16_t>();
          break;
        }
        case 3:
        {
          hl_id = seq.ptr[1].as<std::uint16_t>();
          repeat = seq.ptr[2].as<std::uint32_t>();
          break;
        }
      }
      //std::cout << "Code point: " << c << "\n";
      set_text(g, std::move(text), start_row, col, hl_id, repeat, is_dbl);
      col += repeat + is_dbl; // 0 if not, 1 if it is
    }
    //ss << '\n';
    // Update the area that we modified
    // Translating rows and cols to a pixel area
    //QRect rect = to_pixels(grid_num, start_row, start_col, start_row + 1, col);
    events.push(PaintEventItem {PaintKind::Draw, grid_num, {start_col, start_row, (col - start_col), 1}});
  }
  //std::cout << ss.str() << '\n';
  update();
}

void EditorArea::grid_cursor_goto(const msgpack::object* obj, std::uint32_t size)
{
  assert(obj->type == msgpack::type::ARRAY);
  const auto& arr = obj->via.array;
  assert(arr.size == 3);
  std::uint16_t grid_num = arr.ptr[0].as<std::uint16_t>();
  int row = arr.ptr[1].as<int>();
  int col = arr.ptr[2].as<int>();
  Grid* grid = find_grid(grid_num);
  if (!grid) return;
  neovim_cursor.go_to({grid_num, grid->x, grid->y, row, col});
  // In our paintEvent we will always redraw the current cursor.
  // But we have to get rid of the old cursor (if it's there)
  auto old_pos = neovim_cursor.old_pos();
  if (!old_pos.has_value()) return;
  QRect rect {old_pos->col, old_pos->row, 1, 0};
  events.push({PaintKind::Draw, old_pos->grid_num, rect});
  update();
}

void EditorArea::option_set(const msgpack::object* obj, std::uint32_t size)
{
  for(std::uint32_t i = 0; i < size; ++i)
  {
    const msgpack::object& o = obj[i];
    assert(o.type == msgpack::type::ARRAY);
    const auto& options = o.via.array;
    assert(options.size == 2);
    assert(options.ptr[0].type == msgpack::type::STR);
    std::string opt = options.ptr[0].as<std::string>();
    if (opt == "guifont")
    {
      set_guifont(options.ptr[1].as<QString>());
      font.setHintingPreference(QFont::PreferFullHinting);
    }
    else if (opt == "linespace")
    {
      linespace = options.ptr[1].as<int>();
      update_font_metrics();
      resized(this->size());
    }
  }
}

void EditorArea::flush()
{
}

void EditorArea::win_pos(const msgpack::object* obj)
{
  using u16 = std::uint16_t;
  assert(obj->type == msgpack::type::ARRAY);
  const msgpack::object_array& arr = obj->via.array;
  assert(arr.size == 6);
  const u16 grid_num = arr.ptr[0].as<u16>();
  const u16 sr = arr.ptr[2].as<u16>();
  const u16 sc = arr.ptr[3].as<u16>();
  const u16 width = arr.ptr[4].as<u16>();
  const u16 height = arr.ptr[5].as<u16>();
  Grid* grid = find_grid(grid_num);
  assert(grid);
  grid->hidden = false;
  grid->cols = width;
  grid->rows = height;
  grid->y = sr;
  grid->x = sc;
  grid->area.resize(width * height);
}

static std::tuple<QString, double, std::uint8_t> parse_guifont(const QString& str)
{
  const QStringList list = str.split(":");
  // 1st is font name, 2nd is size, anything after is bold/italic specifier
  switch(list.size())
  {
    case 1:
      return std::make_tuple(list.at(0), -1, FontOpts::Normal);
    case 2:
    {
      // Substr excluding the first char ('h')
      // to get the number
      const QStringRef size_str {&list.at(1), 1, list.at(1).size() - 1};
      return std::make_tuple(list.at(0), size_str.toDouble(), FontOpts::Normal);
    }
    default:
    {
      const QStringRef size_str {&list.at(1), 1, list.at(1).size() - 1};
      std::uint8_t font_opts = FontOpts::Normal;
      assert(list.size() <= 255);
      for(std::uint8_t i = 0; i < list.size(); ++i)
      {
        if (list.at(i) == QLatin1String("b"))
        {
          font_opts |= FontOpts::Bold;
        }
        else if (list.at(i) == QLatin1String("i"))
        {
          font_opts |= FontOpts::Italic;
        }
        else if (list.at(i) == QLatin1String("u"))
        {
          font_opts |= FontOpts::Underline;
        }
        else if (list.at(i) == QLatin1String("s"))
        {
          font_opts |= FontOpts::Strikethrough;
        }
      }
      return std::make_tuple(list.at(0), size_str.toDouble(), font_opts);
    }
  }
}

void EditorArea::set_guifont(const QString& new_font)
{
  // Can take the form
  // <fontname>, <fontname:h<size>, <fontname>:h<size>:b, <fontname>:b,
  // and you should be able to stack multiple fonts together
  // for fallback.
  // We want to consider the fonts one at a time so we have to first
  // split the text. The delimiter that signifies a new font is a comma.
  const QStringList lst = new_font.split(",");
  // No need for complicated stuff if there's only one font to deal with
  if (lst.size() == 1)
  {
    const auto [font_name, font_size, font_opts] = parse_guifont(lst.at(0));
    font.setFamily(font_name);

    if (font_size > 0)
    {
      font.setPointSizeF(font_size);
    }

    if (font_opts & FontOpts::Bold)
    {
      font.setBold(true);
    }
    if (font_opts & FontOpts::Italic)
    {
      font.setItalic(true);
    }
  }
  update_font_metrics();
  resized(size());
  // TODO: Handle multiple fonts (font fallback)
}


Grid* EditorArea::find_grid(const std::uint16_t grid_num)
{
  const auto grid_it = std::find_if(grids.begin(), grids.end(), [grid_num](const Grid& g) {
    return g.id == grid_num;
  });
  return grid_it == grids.end() ? nullptr : &(*grid_it);
}

QRect EditorArea::to_pixels(
  const std::uint16_t x,
  const std::uint16_t y,
  const std::uint16_t width,
  const std::uint16_t height
)
{
  return {
    x * font_width, y * font_height, width * font_width, height * font_height
  };
}

void EditorArea::update_font_metrics()
{
  QFontMetricsF metrics {font};
  float combined_height = std::max(metrics.height(), metrics.lineSpacing());
  font_height = std::ceil(combined_height) + linespace;
  // NOTE: This will only work for monospace fonts since we're basing every char's
  // spocing off a single char.
  constexpr QChar any_char = 'a';
  font_width = std::round(metrics.horizontalAdvance(any_char) + charspace);
  font.setLetterSpacing(QFont::AbsoluteSpacing, charspace);
}

QSize EditorArea::to_rc(const QSize& pixel_size)
{
  int new_width = pixel_size.width() / font_width;
  int new_height = pixel_size.height() / font_height;
  return {new_width, new_height};
}

void EditorArea::paintEvent(QPaintEvent* event)
{
  std::unordered_set<int> drawn;
  QFontMetrics fm {font};
  event->accept();
  QPainter painter(&pixmap);
  painter.setClipping(false);
  painter.setFont(font);
#ifndef NDEBUG
  using Clock = std::chrono::high_resolution_clock;
  const auto start = Clock::now();
#endif
  while(!events.empty())
  {
    const PaintEventItem& event = events.front();
    const Grid* grid = find_grid(event.grid_num);
    assert(grid);
    if (event.type == PaintKind::Clear)
    {
      //qDebug() << "Clear grid " << grid << "\n";
      clear_grid(painter, *grid, event.rect);
    }
    else if (event.type == PaintKind::Redraw)
    {
      for(const auto& grid : grids)
      {
        draw_grid(painter, grid, QRect(0, 0, grid.cols, grid.rows), drawn);
      }
    }
    else
    {
      draw_grid(painter, *grid, event.rect, drawn);
    }
    events.pop();
  }
  QPainter p(this);
  p.drawPixmap(rect(), pixmap, rect());
  if (!neovim_cursor.hidden())
  {
    draw_cursor(p);
  }
#ifndef NDEBUG
  const auto end = Clock::now();
  std::cout << "Grid draw took " << std::chrono::duration<double, std::milli>(end - start).count() << "ms.\n";
#endif
}

std::tuple<std::uint16_t, std::uint16_t> EditorArea::font_dimensions() const
{
  return std::make_tuple(font_width, font_height);
}

void EditorArea::resized(QSize sz)
{
  Q_UNUSED(sz);
  const QSize new_rc = to_rc(size());
  assert(nvim);
  if (new_rc.width() == cols && new_rc.height() == rows) return;
  else
  {
    cols = new_rc.width();
    rows = new_rc.height();
  }
  nvim->resize(new_rc.width(), new_rc.height());
}

void EditorArea::draw_grid(QPainter& painter, const Grid& grid, const QRect& rect, std::unordered_set<int>& drawn_rows)
{
  QString buffer;
  buffer.reserve(100);
  const int start_x = rect.x();
  const int start_y = rect.y();
  const int end_x = rect.right();
  const int end_y = rect.bottom();
  const HLAttr& def_clrs = state->default_colors_get();
  const QFontMetrics metrics {font};
  const int offset = get_offset(font, linespace);
  const auto get_pos = [&](int x, int y, int num_chars) {
    float left = x * font_width;
    float top = y * font_height;
    float bottom = top + font_height;
    float right = left + (font_width * num_chars);
    return std::make_tuple(QPointF(left, top), QPointF(right, bottom));
  };
  const auto draw_buf = [&](QString& text, const HLAttr& attr, const HLAttr& def_clrs, const QPointF& start, const QPointF& end)
  {
    if (!text.isEmpty())
    {
      draw_text_and_bg(painter, text, attr, def_clrs, start, end, offset);
      text.clear();
    }
  };
  for(int y = start_y; y <= end_y && y < grid.rows; ++y)
  {
    QPointF start = {(double) grid.x * font_width, (double) (grid.y + y) * font_height};
    if (drawn_rows.contains(grid.y + y)) continue;
    else drawn_rows.insert(grid.y + y);
    std::uint16_t prev_hl_id = UINT16_MAX;
    for(int x = 0; x < grid.cols; ++x)
    {
      const auto& gc = grid.area[y * grid.cols + x];
      if (gc.hl_id == prev_hl_id)
      {
        buffer.append(gc.text);
        continue;
      }
      else
      {
        auto [top_left, bot_right] = get_pos(grid.x + x, grid.y + y, 1);
        draw_buf(buffer, state->attr_for_id(prev_hl_id), def_clrs, start, bot_right);
        start = top_left;
        buffer.append(gc.text);
        prev_hl_id = gc.hl_id;
      }
    }
    auto [top_left, bot_right] = get_pos(grid.x + (grid.cols - 1), grid.y + y, 1);
    draw_buf(buffer, state->attr_for_id(prev_hl_id), def_clrs, start, bot_right);
  }
}

void EditorArea::clear_grid(QPainter& painter, const Grid& grid, const QRect& rect)
{
  const HLAttr& def_clrs = state->default_colors_get();
  QColor bg = to_qcolor(def_clrs.background);
  // The rect was given in terms of rows and columns, convert to pixels
  // before filling
  const QRect r = to_pixels(grid.x + rect.x(), grid.y + rect.y(), rect.width(), rect.height());
  painter.fillRect(r, bg);
}

void EditorArea::ignore_next_paint_event()
{
  should_ignore_pevent = true;
}

void EditorArea::grid_clear(const msgpack::object *obj, std::uint32_t size)
{
  assert(obj->type == msgpack::type::ARRAY);
  const auto& arr = obj->via.array;
  assert(arr.size == 1);
  const auto grid_num = arr.ptr[0].as<std::uint16_t>();
  Grid* grid = find_grid(grid_num);
  for(auto& gc : grid->area)
  {
    gc.text = ' ';
    gc.hl_id = 0;
    gc.double_width = false;
  }
  QRect&& r = {grid->x, grid->y, grid->cols, grid->rows};
  events.push(PaintEventItem {PaintKind::Clear, grid_num, r});
}

void EditorArea::grid_scroll(const msgpack::object* obj, std::uint32_t size)
{
  using u16 = std::uint16_t;
  assert(obj->type == msgpack::type::ARRAY);
  const msgpack::object_array& arr = obj->via.array;
  assert(arr.size == 7);
  const u16 grid_num = arr.ptr[0].as<u16>();
  const u16 top = arr.ptr[1].as<u16>();
  const u16 bot = arr.ptr[2].as<u16>();
  const u16 left = arr.ptr[3].as<u16>();
  const u16 right = arr.ptr[4].as<u16>();
  const int rows = arr.ptr[5].as<int>();
  const int cols = arr.ptr[6].as<int>();
  Grid* grid = find_grid(grid_num);
  if (!grid) return;
  assert(grid);
  if (rows > 0)
  {
    for(int y = top; y < (bot-rows) && y < (grid->rows - rows); ++y)
    {
      for(int x = left; x < right && x < grid->cols; ++x)
      {
        grid->area[y * grid->cols + x] = std::move(grid->area[(y + rows) * grid->cols + x]);
      }
    }
  }
  else if (rows < 0)
  {
    for(int y = (bot-1); y > top && y >= -rows; --y)
    {
      for(int x = left; x <= right && x < grid->cols; ++x)
      {
        grid->area[y * grid->cols + x] = std::move(grid->area[(y + rows) * grid->cols + x]);
      }
    }
  }
  auto rect = QRect(left, top, (right - left), (bot - top));
  events.push(PaintEventItem {PaintKind::Draw, grid_num, rect});
}

void EditorArea::mousePressEvent(QMouseEvent* event)
{
  if (cursor() != Qt::ArrowCursor)
  {
    QWidget::mousePressEvent(event);
  }
}

void EditorArea::set_resizing(bool is_resizing)
{
  resizing = is_resizing;
}

std::string event_to_string(QKeyEvent* event, bool* special)
{
  *special = true;
  switch(event->key())
  {
    case Qt::Key_Enter:
      return "CR";
    case Qt::Key_Return:
      return "CR";
    case Qt::Key_Backspace:
      return "BS";
    case Qt::Key_Tab:
      return "Tab";
    case Qt::Key_Down:
      return "Down";
    case Qt::Key_Up:
      return "Up";
    case Qt::Key_Left:
      return "Left";
    case Qt::Key_Right:
      return "Right";
    case Qt::Key_Escape:
      return "Esc";
    case Qt::Key_Home:
      return "Home";
    case Qt::Key_Insert:
      return "Insert";
    case Qt::Key_Delete:
      return "Del";
    case Qt::Key_PageUp:
      return "PageUp";
    case Qt::Key_PageDown:
      return "PageDown";
    case Qt::Key_Less:
      return "LT";
    case Qt::Key_Space:
      return "Space";
    default:
      *special = false;
      return event->text().toStdString();
  }
}

void EditorArea::keyPressEvent(QKeyEvent* event)
{
  event->accept();
  const auto modifiers = event->modifiers();
  bool ctrl = modifiers & Qt::ControlModifier;
  bool shift = modifiers & Qt::ShiftModifier;
  bool alt = modifiers & Qt::AltModifier;
  bool is_special = false;
  const QString& text = event->text();
  std::string key = event_to_string(event, &is_special);
  if (text.isEmpty() || text.at(0).isSpace())
  {
    nvim->send_input(ctrl, shift, alt, std::move(key), is_special);
  }
  // Qt already factors in Shift+Ctrl into a lot of keys.
  // For the number keys, it doesn't factor in Ctrl though.
  else if (text.at(0).isNumber())
  {
    nvim->send_input(ctrl, false, alt, std::move(key), is_special);
  }
  else
  {
    nvim->send_input(false, false, alt, std::move(key), is_special);
  }
}

bool EditorArea::focusNextPrevChild(bool next)
{
  return false;
}

void EditorArea::default_colors_changed(QColor fg, QColor bg)
{
  // Since we draw to an internal bitmap it is better to draw the entire
  // bitmap with the bg color and then send a redraw message.
  // This makes it looks nicer when resizing (otherwise the part that hasn't been
  // drawn yet is completely black)
  // This has to be virtual because we aren't always drawing to a QPixmap
  // (WinEditorArea draws to a ID2D1Bitmap)
  QPainter p(&pixmap);
  p.fillRect(pixmap.rect(), bg);
  events.push({PaintKind::Redraw, 0, QRect()});
}

void EditorArea::mode_info_set(const msgpack::object* obj, std::uint32_t size)
{
  neovim_cursor.mode_info_set(obj, size);
}

void EditorArea::mode_change(const msgpack::object* obj, std::uint32_t size)
{
  neovim_cursor.mode_change(obj, size);
}

void EditorArea::draw_text_and_bg(
  QPainter& painter,
  const QString& text,
  const HLAttr& attr,
  const HLAttr& def_clrs,
  const QPointF& start,
  const QPointF& end,
  const int offset
)
{
  font.setItalic(attr.font_opts & FontOpts::Italic);
  font.setBold(attr.font_opts & FontOpts::Bold);
  font.setUnderline(attr.font_opts & FontOpts::Underline);
  font.setStrikeOut(attr.font_opts & FontOpts::Strikethrough);
  painter.setFont(font);
  Color fg = attr.has_fg ? attr.foreground : def_clrs.foreground;
  Color bg = attr.has_bg ? attr.background : def_clrs.background;
  if (attr.reverse) std::swap(fg, bg);
  const QRectF rect = {start, end};
  painter.setClipRect(rect);
  painter.fillRect(rect, QColor(bg.r, bg.g, bg.b));
  painter.setPen(QColor(fg.r, fg.g, fg.b));
  const QPointF text_start = {start.x(), start.y() + offset};
  painter.drawText(text_start, text);
}

void EditorArea::draw_cursor(QPainter& painter)
{
  QFontMetrics fm {font};
  const int offset = get_offset(font, linespace);
  auto rect_opt = neovim_cursor.rect(font_width, font_height);
  if (rect_opt.has_value())
  {
    auto rect = rect_opt.value();
    const auto pos = neovim_cursor.pos().value();
    Grid* grid = find_grid(pos.grid_num);
    const auto& gc = grid->area[pos.row * grid->cols + pos.col];
    const HLAttr& def_clrs = state->default_colors_get();
    const HLAttr& attr = state->attr_for_id(rect.hl_id);
    Color bg = rect.hl_id == 0 ? def_clrs.foreground : (attr.reverse ? (attr.has_fg ? attr.foreground : def_clrs.foreground) : (attr.has_bg ? attr.background : def_clrs.background));
    painter.fillRect(rect.rect, QColor(bg.r, bg.g, bg.b));
    if (rect.should_draw_text)
    {
      const QPoint bot_left = { (grid->x + pos.col) * font_width, (grid->y + pos.row) * font_height + offset};
      const Color fgc = rect.hl_id == 0 ? def_clrs.background : (attr.reverse ? (attr.has_bg ? attr.background : def_clrs.background) : (attr.has_fg ? attr.foreground : def_clrs.background));
      const QColor fg = {fgc.r, fgc.g, fgc.b};
      painter.setFont(font);
      painter.setPen(fg);
      painter.drawText(bot_left, gc.text);
    }
  }
}
