#include "editor.hpp"
#include "input.hpp"
#include "msgpack_overrides.hpp"
#include "utils.hpp"
#include <chrono>
#include <limits>
#include <QApplication>
#include <QDebug>
#include <QDesktopWidget>
#include <QFileInfo>
#include <QMimeData>
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

using u16 = std::uint16_t;
using u32 = std::uint32_t;

static float get_offset(const QFont& font, const float linespacing)
{
  QFontMetricsF fm {font};
  return fm.ascent() + (linespacing / 2);
}

/**
 * Sets relative's point size to a point size that is such that the horizontal
 * advance of the character 'a' is within tolerance of the target's horizontal
 * advance for the same character.
 * This is done using a binary search algorithm between
 * (0, target.pointSizeF() * 2.). The algorithm runs in a loop, the number
 * of times can be limited using max_iterations. If max_iterations is 0
 * the loop will run without stopping until it is within error.
 */
static void set_relative_font_size(
  const QFont& target,
  QFont& modified,
  const double tolerance,
  const u32 max_iterations
)
{ 
  constexpr auto width = [](const QFontMetricsF& m) {
    return m.horizontalAdvance('a');
  };
  QFontMetricsF target_metrics {target};
  const double target_width = width(target_metrics);
  double low = 0.;
  double high = target.pointSizeF() * 2.;
  modified.setPointSizeF(high);
  for(u32 rep = 0;
      (rep < max_iterations || max_iterations == 0) && low <= high;
      ++rep)
  {
    double mid = (low + high) / 2.;
    modified.setPointSizeF(mid);
    QFontMetricsF metrics {modified};
    const double diff =  target_width - width(metrics);
    if (std::abs(diff) <= tolerance) return;
    if (diff < 0) /** point size too big */ high = mid;
    else if (diff > 0) /** point size too low */ low = mid;
    else return;
  }
}

EditorArea::EditorArea(QWidget* parent, HLState* hl_state, Nvim* nv)
: QWidget(parent),
  state(hl_state),
  nvim(nv),
  pixmap(width(), height()),
  neovim_cursor(this),
  popup_menu(hl_state, this),
  cmdline(hl_state, &neovim_cursor, this),
  mouse(QApplication::doubleClickInterval())
{
  setAttribute(Qt::WA_InputMethodEnabled);
  setAttribute(Qt::WA_OpaquePaintEvent);
  setAutoFillBackground(false);
  setAcceptDrops(true);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setFocusPolicy(Qt::StrongFocus);
  setFocus();
  setMouseTracking(true);
  font.setPointSizeF(11.25);
  fonts.push_back({font});
  update_font_metrics(true);
  QObject::connect(&neovim_cursor, &Cursor::cursor_hidden, this, [this] {
    update();
  });
  QObject::connect(&neovim_cursor, &Cursor::cursor_visible, this, [this] {
    update();
  });
}

void EditorArea::set_text(
  GridBase& grid,
  grid_char c,
  std::uint16_t row,
  std::uint16_t col,
  std::uint16_t hl_id,
  std::uint16_t repeat,
  bool is_dbl_width
)
{
  u32 ucs;
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
  //std::cout << "Set " << repeat << " texts at (" << row << ", " << col << ").\n";
  // Neovim should make sure this isn't out-of-bounds
  assert(col + repeat <= grid.cols);
  for(std::uint16_t i = 0; i < repeat; ++i)
  {
    // row * grid.cols - get current row
    assert(static_cast<std::size_t>(row * grid.cols + col + i) < grid.area.size());
    grid.area[row * grid.cols + col + i] = {hl_id, c, is_dbl_width, ucs};
  }
}

void EditorArea::grid_resize(const msgpack::object *obj, u32 size)
{
  // Should only run once
  for(u32 i = 0; i < size; ++i)
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
    GridBase* grid = find_grid(grid_num);
    if (grid)
    {
      grid->set_size(width, height);
    }
    else
    {
      create_grid(0, 0, width, height, grid_num);
      grid = find_grid(grid_num);
      // Created grid appears above all others
      grid->z_index = grids.size() - 1;
    }
    if (grid)
    {
      send_draw(grid_num, {0, 0, grid->cols, grid->rows});
    }
  }
}

void EditorArea::grid_line(NeovimObj obj, u32 size)
{
  std::uint16_t hl_id = 0;
  //std::cout << "Received grid line.\nNum params: " << size << '\n';
  for(u32 i = 0; i < size; ++i)
  {
    const msgpack::object& grid_cmd = obj[i];
    assert(grid_cmd.type == msgpack::type::ARRAY);
    const auto& grid = grid_cmd.via.array;
    assert(grid.size == 4);
    const std::uint16_t grid_num = grid.ptr[0].as<std::uint16_t>();
    //std::cout << "Grid line on grid " << grid_num << '\n';
    // Get associated grid
    GridBase* grid_ptr = find_grid(grid_num);
    if (!grid_ptr) return;
    GridBase& g = *grid_ptr;
    const std::uint16_t start_row = grid.ptr[1].as<std::uint16_t>();
    const std::uint16_t start_col = grid.ptr[2].as<std::uint16_t>();
    int col = start_col;
    const msgpack::object& cells_obj = grid.ptr[3];
    assert(cells_obj.type == msgpack::type::ARRAY);
    const auto& cells = cells_obj.via.array;
    for(u32 j = 0; j < cells.size; ++j)
    {
      // [text, (hl_id, repeat)]
      const msgpack::object& o = cells.ptr[j];
      assert(o.type == msgpack::type::ARRAY);
      const auto& seq = o.via.array;
      assert(seq.size >= 1 && seq.size <= 3);
      int repeat = 1;
      assert(seq.ptr[0].type == msgpack::type::STR);
      grid_char text = seq.ptr[0].as<decltype(text)>();
      // If the previous char was a double-width char,
      // the current char is an empty string.
      bool prev_was_dbl = text.isEmpty();
      //bool is_dbl = !text.isEmpty() && fm.horizontalAdvance(text) != font_width;
      bool is_dbl = false;
      if (prev_was_dbl)
      {
        std::size_t idx = start_row * grid_ptr->cols + col - 1;
        if (idx < grid_ptr->area.size())
        {
          grid_ptr->area[idx].double_width = true;
        }
      }
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
          repeat = seq.ptr[2].as<u32>();
          break;
        }
      }
      //std::cout << "Code point: " << c << "\n";
      g.set_text(std::move(text), start_row, col, hl_id, repeat, is_dbl);
      col += repeat;
    }
    //ss << '\n';
    // Update the area that we modified
    // Translating rows and cols to a pixel area
    //QRect rect = to_pixels(grid_num, start_row, start_col, start_row + 1, col);
    send_draw(grid_num, {start_col, start_row, (col - start_col), 1});
  }
  //update();
}

void EditorArea::grid_cursor_goto(const msgpack::object* obj, u32 size)
{
  Q_UNUSED(size);
  assert(obj->type == msgpack::type::ARRAY);
  const auto& arr = obj->via.array;
  assert(arr.size == 3);
  std::uint16_t grid_num = arr.ptr[0].as<std::uint16_t>();
  int row = arr.ptr[1].as<int>();
  int col = arr.ptr[2].as<int>();
  GridBase* grid = find_grid(grid_num);
  if (!grid) return;
  neovim_cursor.go_to({grid_num, grid->x, grid->y, row, col});
  // In our paintEvent we will always redraw the current cursor.
  // But we have to get rid of the old cursor (if it's there)
  auto old_pos = neovim_cursor.old_pos();
  if (!old_pos.has_value()) return;
  QRect rect {old_pos->col, old_pos->row, 1, 0};
  send_draw(old_pos->grid_num, rect);
  // Thanks Neovim-Qt
  // https://github.com/equalsraf/neovim-qt/blob/4212ee18a7536c5e2ed101fc1aeed610c502c0df/src/gui/shell.cpp#L1203-L1220
  qApp->inputMethod()->update(Qt::ImCursorRectangle);
  //update();
}

void EditorArea::option_set(const msgpack::object* obj, u32 size)
{
  static const auto extension_for = [&](const std::string& s) -> bool* {
    if (s == "ext_linegrid") return &capabilities.linegrid;
    else if (s == "ext_popupmenu") return &capabilities.popupmenu;
    else if (s == "ext_cmdline") return &capabilities.cmdline;
    else if (s == "ext_multigrid") return &capabilities.multigrid;
    else if (s == "ext_wildmenu") return &capabilities.wildmenu;
    else if (s == "ext_messages") return &capabilities.messages;
    else return nullptr;
  };
  for(u32 i = 0; i < size; ++i)
  {
    const msgpack::object& o = obj[i];
    assert(o.type == msgpack::type::ARRAY);
    const auto& options = o.via.array;
    assert(options.size == 2);
    assert(options.ptr[0].type == msgpack::type::STR);
    std::string opt = options.ptr[0].as<std::string>();
    if (bool* capability = extension_for(opt); capability)
    {
      *capability = options.ptr[1].as<bool>();
    }
    else if (opt == "guifont")
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
  //for(auto& grid : grids)
  //{
    //fmt::print(
      //"ID: {}, X: {}, Y: {}, Width: {}, Height: {}\n",
      //grid->id, grid->x, grid->y, grid->cols, grid->rows
    //);
  //}
  sort_grids_by_z_index();
  update();
}

void EditorArea::win_pos(NeovimObj obj, u32 size)
{
  using u16 = std::uint16_t;
  for(u32 i = 0; i < size; ++i)
  {
    const auto& o = obj[i];
    assert(o.type == msgpack::type::ARRAY);
    const msgpack::object_array& arr = o.via.array;
    assert(arr.size == 6);
    const u16 grid_num = arr.ptr[0].as<u16>();
    const u16 sr = arr.ptr[2].as<u16>();
    const u16 sc = arr.ptr[3].as<u16>();
    const u16 width = arr.ptr[4].as<u16>();
    const u16 height = arr.ptr[5].as<u16>();
    GridBase* grid = find_grid(grid_num);
    if (!grid)
    {
      fmt::print("No grid #{} found.\n", grid_num);
      continue;
    }
    shift_z(grid->z_index);
    grid->hidden = false;
    grid->set_size(width, height);
    grid->win_pos(sc, sr);
    grid->z_index = grids.size() - 1;
    QRect r(0, 0, grid->cols, grid->rows);
  }
  send_redraw();
}

void EditorArea::win_hide(NeovimObj obj, u32 size)
{
  for(u32 i = 0; i < size; ++i)
  {
    const auto& o = obj[i];
    assert(o.type == msgpack::type::ARRAY);
    const auto& arr = o.via.array;
    assert(arr.size >= 2);
    auto grid_num = arr.ptr[0].as<u16>();
    if (GridBase* grid = find_grid(grid_num); grid) grid->hidden = true;
  }
}

void EditorArea::win_float_pos(NeovimObj obj, u32 size)
{
  for(u32 i = 0; i < size; ++i)
  {
    const auto& o = obj[i];
    assert(o.type == msgpack::type::ARRAY);
    const auto& arr = o.via.array;
    assert(arr.size >= 7);
    auto grid_num = arr.ptr[0].as<u16>();
    auto anchor_dir = arr.ptr[2].as<std::string>();
    auto anchor_grid_num = arr.ptr[3].as<u16>();
    // NOTE: I have absolutely no idea why these are
    // float types, but they are.
    int anchor_row = arr.ptr[4].as<double>();
    int anchor_col = arr.ptr[5].as<double>();
    QPoint anchor_rel = {anchor_col, anchor_row};
    //bool focusable = arr.ptr[6].as<bool>();
    GridBase* grid = find_grid(grid_num);
    GridBase* anchor_grid = find_grid(anchor_grid_num);
    if (!grid || !anchor_grid) return;
    // Anchor dir is "NW", "SW", "SE", "NE"
    // The anchor direction indicates which corner of the grid
    // should be at the position given.
    QPoint anchor_pos = anchor_grid->top_left() + anchor_rel;
    if (anchor_dir == "SW")
    {
      anchor_pos -= QPoint(0, grid->rows);
    }
    else if (anchor_dir == "SE")
    {
      anchor_pos -= QPoint(grid->cols, grid->rows);
    }
    else if (anchor_dir == "NE")
    {
      anchor_pos -= QPoint(grid->cols, 0);
    }
    else
    {
      // NW, no need to do anything
    }
    if (!popup_menu.hidden() && popup_menu.selected_idx() != -1)
    {
      QPoint pum_tr = popupmenu_rect().topRight();
      // Don't let the grid get clipped by the popup menu
      float pum_rx = std::round(pum_tr.x() / font_width);
      float pum_ty = std::ceil(pum_tr.y() / font_height);
      anchor_pos = QPoint(pum_rx, pum_ty);
    }
    shift_z(grid->z_index);
    bool were_animations_enabled = animations_enabled();
    set_animations_enabled(false);
    grid->float_pos(anchor_pos.x(), anchor_pos.y());
    grid->z_index = grids.size() - 1;
    set_animations_enabled(were_animations_enabled);
  }
}

void EditorArea::win_close(NeovimObj obj, u32 size)
{
  for(u32 i = 0; i < size; ++i)
  {
    const auto& o = obj[i];
    assert(o.type == msgpack::type::ARRAY);
    const auto& arr = o.via.array;
    assert(arr.size >= 1);
    auto grid_num = arr.ptr[0].as<u16>();
    destroy_grid(grid_num);
  }
  send_redraw();
}

void EditorArea::win_viewport(NeovimObj obj, u32 size)
{
  for(u32 i = 0; i < size; ++i)
  {
    const auto& o = obj[i];
    assert(o.type == msgpack::type::ARRAY);
    const auto& arr = o.via.array;
    assert(arr.size >= 6);
    auto grid_num = arr.ptr[0].as<u16>();
    auto topline = arr.ptr[2].as<u32>();
    auto botline = arr.ptr[3].as<u32>();
    auto curline = arr.ptr[4].as<u32>();
    auto curcol = arr.ptr[5].as<u32>();
    Viewport vp = {topline, botline, curline, curcol};
    auto* grid = find_grid(grid_num);
    if (!grid) continue;
    grid->viewport_changed(std::move(vp));
  }
}

void EditorArea::grid_destroy(NeovimObj obj, u32 size)
{
  for(u32 i = 0; i < size; ++i)
  {
    const auto& o = obj[i];
    assert(o.type == msgpack::type::ARRAY);
    const auto& arr = o.via.array;
    assert(arr.size >= 1);
    auto grid_num = arr.ptr[0].as<u16>();
    destroy_grid(grid_num);
  }
  send_redraw();
}

void EditorArea::msg_set_pos(NeovimObj obj, u32 size)
{
  for(u32 i = 0; i < size; ++i)
  {
    const auto& o = obj[i];
    assert(o.type == msgpack::type::ARRAY);
    const auto& arr = o.via.array;
    assert(arr.size >= 4);
    auto grid_num = arr.ptr[0].as<u16>();
    auto row = arr.ptr[1].as<u16>();
    //auto scrolled = arr.ptr[2].as<bool>();
    //auto sep_char = arr.ptr[3].as<QString>();
    if (GridBase* grid = find_grid(grid_num); grid)
    {
      shift_z(grid->z_index);
      grid->set_pos(grid->x, row);
      grid->z_index = grids.size() - 1;
    }
  }
  send_redraw();
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
      const QStringView size_str {list.at(1).utf16() + 1, list[1].size() - 1};
      return std::make_tuple(list.at(0), size_str.toDouble(), FontOpts::Normal);
    }
    default:
    {
      const QStringView size_str {list.at(1).utf16() + 1, list[1].size() - 1};
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

void EditorArea::set_guifont(QString new_font)
{
  // Can take the form
  // <fontname>, <fontname:h<size>, <fontname>:h<size>:b, <fontname>:b,
  // and you should be able to stack multiple fonts together
  // for fallback.
  // We want to consider the fonts one at a time so we have to first
  // split the text. The delimiter that signifies a new font is a comma.

  // Neovim sends "" on initialization, but we want a monospace font
  if (new_font.isEmpty())
  {
    new_font = default_font_family();
  }
  const QStringList lst = new_font.split(",");
  fonts.clear();
  font_for_unicode.clear();
  // No need for complicated stuff if there's only one font to deal with
  if (lst.size() == 0) return;
  auto [font_name, font_size, font_opts] = parse_guifont(lst.at(0));
  QFontDatabase font_db;
  const auto set_font_if_contains = [&](QFont& f, const QString& family) {
    if (font_db.hasFamily(family)) f.setFamily(family);
    else
    {
      nvim->err_write(fmt::format(
        "Could not find font for family \"{}\".\n",
        family.toStdString()
      ));
      f.setFamily(default_font_family());
    }
  };
  set_font_if_contains(font, font_name);
  if (font_size > 0)
  {
    font.setPointSizeF(font_size);
  }
  font.setBold(font_opts & FontOpts::Bold);
  font.setItalic(font_opts & FontOpts::Italic);
  fonts.push_back({font});
  for(int i = 1; i < lst.size(); ++i)
  {
    std::tie(font_name, font_size, font_opts) = parse_guifont(lst[i]);
    QFont f;
    set_font_if_contains(f, font_name);
    set_relative_font_size(font, f, 0.0001, 1000);
    // The widths at the same point size can be different,
    // normalize the widths
    Font fo = f;
    fonts.push_back(std::move(fo));
  }
  update_font_metrics(true);
  resized(size());
  send_redraw();
  emit font_changed();
}


GridBase* EditorArea::find_grid(const std::uint16_t grid_num)
{
  const auto grid_it = std::find_if(grids.begin(), grids.end(), [grid_num](const auto& g) {
    return g->id == grid_num;
  });
  return grid_it == grids.end() ? nullptr : grid_it->get();
}

QRect EditorArea::to_pixels(
  const std::uint16_t x,
  const std::uint16_t y,
  const std::uint16_t width,
  const std::uint16_t height
)
{
  return QRect(
    x * font_width, y * font_height, width * font_width, height * font_height
  );
}

void EditorArea::update_font_metrics(bool)
{
  QFontMetricsF metrics {font};
  float combined_height = std::max(metrics.height(), metrics.lineSpacing());
  font_height = combined_height + linespace;
  // NOTE: This will only work for monospace fonts since we're basing every char's
  // spocing off a single char.
  constexpr QChar any_char = 'a';
  font_width = metrics.horizontalAdvance(any_char) + charspace;
  font.setLetterSpacing(QFont::AbsoluteSpacing, charspace);
  for(auto& f : fonts)
  {
    QFont old_font = f.font();
    old_font.setLetterSpacing(QFont::AbsoluteSpacing, charspace);
    f = old_font;
  }
  popup_menu.font_changed(font, font_width, font_height, linespace);
}

QSize EditorArea::to_rc(const QSize& pixel_size)
{
  int new_width = pixel_size.width() / font_width;
  int new_height = pixel_size.height() / font_height;
  return {new_width, new_height};
}

void EditorArea::paintEvent(QPaintEvent* event)
{
  Q_UNUSED(event);
  QPainter p(this);
  p.fillRect(rect(), default_bg());
  QRectF grid_clip_rect(0, 0, cols * font_width, rows * font_height);
  p.setClipRect(grid_clip_rect);
  for(auto& grid_base : grids)
  {
    auto* grid = static_cast<QPaintGrid*>(grid_base.get());
    if (!grid->hidden)
    {
      QSize size = grid->buffer().size();
      auto r = QRectF(grid->pos(), size).intersected(grid_clip_rect);
      p.setClipRect(r);
      grid->process_events();
      grid->render(p);
    }
  }
  p.setClipRect(rect());
  if (!neovim_cursor.hidden() && cmdline.isHidden())
  {
    draw_cursor(p);
  }
  if (!popup_menu.hidden())
  {
    draw_popup_menu();
  } else popup_menu.setVisible(false);
}

std::tuple<float, float> EditorArea::font_dimensions() const
{
  return std::make_tuple(font_width, font_height);
}

void EditorArea::resized(QSize sz)
{
  Q_UNUSED(sz);
  const QSize new_rc = to_rc(size());
  assert(nvim);
  if (resizing)
  {
    queued_resize = new_rc;
    return;
  }
  if (new_rc.width() == cols && new_rc.height() == rows) return;
  else
  {
    cols = new_rc.width();
    rows = new_rc.height();
  }
  cmdline.parent_resized(size());
  popup_menu.cmdline_width_changed(cmdline.width());
  reposition_cmdline();
  resizing = true;
  nvim->resize_cb(new_rc.width(), new_rc.height(), [&](auto res, auto err) {
    Q_UNUSED(res); Q_UNUSED(err);
    QMetaObject::invokeMethod(this, [this] {
      resizing = false;
      if (queued_resize)
      {
        resized(queued_resize.value());
        queued_resize.reset();
      }
    });
  });
}

void EditorArea::draw_grid(QPainter& painter, const GridBase& grid, const QRect& rect)
{
  QString buffer;
  buffer.reserve(100);
  // const int start_x = rect.x();
  const int start_y = rect.y();
  // const int end_x = rect.right();
  const int end_y = rect.bottom();
  const HLAttr& def_clrs = state->default_colors_get();
  const float offset = get_offset(font, linespace);
  const auto get_pos = [&](int x, int y, int num_chars) {
    float left = x * font_width;
    float top = y * font_height;
    float bottom = top + font_height;
    float right = left + (font_width * num_chars);
    return std::make_tuple(QPointF(left, top), QPointF(right, bottom));
  };
  QFont cur_font = font;
  const auto draw_buf = [&](QString& text, const HLAttr& attr, const HLAttr& def_clrs,
      const QPointF& start, const QPointF& end, u32 font_idx) {
    if (!text.isEmpty())
    {
      cur_font = fonts[font_idx].font();
      draw_text_and_bg(painter, text, attr, def_clrs, start, end, offset, cur_font);
      text.clear();
    }
  };
  for(int y = start_y; y <= end_y && y < grid.rows; ++y)
  {
    QPointF start(grid.x * font_width, (grid.y + y) * font_height);
    std::uint16_t prev_hl_id = UINT16_MAX;
    u32 cur_font_idx = 0;
    for(int x = 0; x < grid.cols; ++x)
    {
      const auto& gc = grid.area[y * grid.cols + x];
      auto font_idx = font_for_ucs(gc.ucs);
      if (font_idx != cur_font_idx && !(gc.text.isEmpty() || gc.text.at(0).isSpace()))
      {
        auto [top_left, bot_right] = get_pos(grid.x + x, grid.y + y, 1);
        QPointF buf_end = {top_left.x(), top_left.y() + font_height};
        draw_buf(buffer, state->attr_for_id(prev_hl_id), def_clrs, start, buf_end, cur_font_idx);
        start = top_left;
        cur_font_idx = font_idx;
      }
      if (gc.double_width)
      {
        auto [top_left, bot_right] = get_pos(grid.x + x, grid.y + y, 2);
        QPointF buf_end = {top_left.x(), top_left.y() + font_height};
        draw_buf(buffer, state->attr_for_id(prev_hl_id), def_clrs, start, buf_end, cur_font_idx);
        buffer.append(gc.text);
        draw_buf(buffer, state->attr_for_id(gc.hl_id), def_clrs, top_left, bot_right, cur_font_idx);
        start = {bot_right.x(), bot_right.y() - font_height};
        prev_hl_id = gc.hl_id;
      }
      else if (gc.hl_id == prev_hl_id)
      {
        buffer.append(gc.text);
        continue;
      }
      else
      {
        auto [top_left, bot_right] = get_pos(grid.x + x, grid.y + y, 1);
        draw_buf(buffer, state->attr_for_id(prev_hl_id), def_clrs, start, bot_right, cur_font_idx);
        start = top_left;
        buffer.append(gc.text);
        prev_hl_id = gc.hl_id;
      }
    }
    auto [top_left, bot_right] = get_pos(grid.x + (grid.cols - 1), grid.y + y, 1);
    draw_buf(buffer, state->attr_for_id(prev_hl_id), def_clrs, start, bot_right, cur_font_idx);
  }
}

void EditorArea::clear_grid(QPainter& painter, const GridBase& grid, const QRect& rect)
{
  const HLAttr& def_clrs = state->default_colors_get();
  QColor bg = def_clrs.background->to_uint32();
  // The rect was given in terms of rows and columns, convert to pixels
  // before filling
  const QRect r = to_pixels(grid.x + rect.x(), grid.y + rect.y(), rect.width(), rect.height());
  painter.fillRect(r, bg);
}

void EditorArea::ignore_next_paint_event()
{
  should_ignore_pevent = true;
}

void EditorArea::grid_clear(const msgpack::object *obj, u32 size)
{
  Q_UNUSED(size);
  assert(obj->type == msgpack::type::ARRAY);
  const auto& arr = obj->via.array;
  assert(arr.size == 1);
  const auto grid_num = arr.ptr[0].as<std::uint16_t>();
  GridBase* grid = find_grid(grid_num);
  for(auto& gc : grid->area)
  {
    gc = {0, " ", false, QChar(' ').unicode()};
  }
  QRect&& r = {grid->x, grid->y, grid->cols, grid->rows};
  send_clear(grid_num, r);
}

void EditorArea::grid_scroll(const msgpack::object* obj, u32 size)
{
  Q_UNUSED(size);
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
  // const int cols = arr.ptr[6].as<int>();
  GridBase* grid = find_grid(grid_num);
  if (!grid) return;
  assert(grid);
  if (rows > 0)
  {
    for(int y = top; y < (bot - rows); ++y)
    {
      for(int x = left; x < right && x < grid->cols; ++x)
      {
        grid->area[y * grid->cols + x] = std::move(grid->area[(y + rows) * grid->cols + x]);
      }
    }
  }
  else if (rows < 0)
  {
    for(int y = (bot-1); y >= (top - rows); --y)
    {
      for(int x = left; x <= right && x < grid->cols; ++x)
      {
        grid->area[y * grid->cols + x] = std::move(grid->area[(y + rows) * grid->cols + x]);
      }
    }
  }
  auto rect = QRect(left, top, (right - left), (bot - top));
  send_draw(grid_num, rect);
}

static std::string mouse_button_to_string(Qt::MouseButtons btn)
{
  switch(btn)
  {
    case Qt::LeftButton:
      return std::string("left");
    case Qt::RightButton:
      return std::string("right");
    case Qt::MiddleButton:
      return std::string("middle");
    default:
      return std::string("");
  }
}

static std::string mouse_mods_to_string(
  Qt::KeyboardModifiers mods,
  int click_count = 0
)
{
  std::string mod_str;
  if (mods & Qt::ControlModifier) mod_str.push_back('c');
  if (mods & Qt::AltModifier) mod_str.push_back('a');
  if (mods & Qt::ShiftModifier) mod_str.push_back('s');
  if (click_count > 1) mod_str.append(std::to_string(click_count));
  return mod_str;
}

void EditorArea::set_resizing(bool is_resizing)
{
  resizing = is_resizing;
}

void EditorArea::keyPressEvent(QKeyEvent* event)
{
  if (hide_cursor_while_typing && cursor() != Qt::BlankCursor)
  {
    setCursor(Qt::BlankCursor);
  }
  event->accept();
  auto text = convert_key(*event);
  if (text.empty()) return;
  nvim->send_input(std::move(text));
}

bool EditorArea::focusNextPrevChild(bool /* is_next */)
{
  return false;
}

void EditorArea::default_colors_changed(QColor fg, QColor bg)
{
  Q_UNUSED(fg);
  Q_UNUSED(bg);
  // Since we draw to an internal bitmap it is better to draw the entire
  // bitmap with the bg color and then send a redraw message.
  // This makes it looks nicer when resizing (otherwise the part that hasn't been
  // drawn yet is completely black)
  // This has to be virtual because we aren't always drawing to a QPixmap
  // (WinEditorArea draws to a ID2D1Bitmap)
  send_redraw();
}

void EditorArea::mode_info_set(const msgpack::object* obj, u32 size)
{
  neovim_cursor.mode_info_set(obj, size);
}

void EditorArea::mode_change(const msgpack::object* obj, u32 size)
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
  const float offset,
  QFont font
)
{
  font.setItalic(attr.font_opts & FontOpts::Italic);
  font.setBold(attr.font_opts & FontOpts::Bold);
  font.setUnderline(attr.font_opts & FontOpts::Underline);
  font.setStrikeOut(attr.font_opts & FontOpts::Strikethrough);
  painter.setFont(font);
  Color fg = attr.fg().value_or(*def_clrs.fg());
  Color bg = attr.bg().value_or(*def_clrs.bg());
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
  auto pos_opt = neovim_cursor.pos();
  if (!pos_opt) return;
  auto p = pos_opt.value();
  GridBase* grid = find_grid(p.grid_num);
  if (!grid) return;
  std::size_t idx = p.row * grid->cols + p.col;
  if (idx >= grid->area.size()) return;
  const auto& gc = grid->area[idx];
  float scale_factor = 1.0f;
  if (gc.double_width) scale_factor = 2.0f;
  auto rect = neovim_cursor.rect(font_width, font_height, scale_factor).value();
  QFontMetrics fm {font};
  const float offset = get_offset(font, linespace);
  const auto pos = neovim_cursor.pos().value();
  const HLAttr& def_clrs = state->default_colors_get();
  const HLAttr& attr = state->attr_for_id(rect.hl_id);
  Color bg = rect.hl_id == 0 ? *def_clrs.fg() : (attr.reverse ? (attr.fg().value_or(*def_clrs.fg())) : (attr.bg().value_or(*def_clrs.bg())));
  painter.fillRect(rect.rect, QColor(bg.r, bg.g, bg.b));
  if (rect.should_draw_text)
  {
    float left = (grid->x + pos.col) * font_width;
    float top = (grid->y + pos.row) * font_height + offset;
    const QPointF bot_left {left, top};
    const Color fgc = rect.hl_id == 0
      ? *def_clrs.bg()
      : (attr.reverse
          ? (attr.bg().value_or(*def_clrs.bg()))
          : attr.fg().value_or(*def_clrs.fg()));
    const QColor fg = {fgc.r, fgc.g, fgc.b};
    auto font_idx = font_for_ucs(gc.ucs);
    painter.setFont(fonts[font_idx].font());
    painter.setPen(fg);
    painter.drawText(bot_left, gc.text);
  }
}

QRect EditorArea::popupmenu_rect()
{
  QRect popup_rect = popup_menu.available_rect();
  auto&& [grid_num, row, col] = popup_menu.position();
  auto&& [font_width, font_height] = font_dimensions();
  bool is_cmdline = grid_num == -1;
  int start_x, start_y;
  if (is_cmdline)
  {
    QPoint cmdline_pos = cmdline.popupmenu_pt(popup_rect.height(), size());
    start_x = cmdline_pos.x();
    start_y = cmdline_pos.y();
  }
  else
  {
    GridBase* grid = find_grid(grid_num);
    if (!grid) return {};
    start_x = (grid->x + col) * font_width;
    start_y = (grid->y + row + 1) * font_height;
    // int p_width = popup_rect.width();
    int p_height = popup_rect.height();
    if (start_y + p_height > height() && (start_y - p_height - font_height) >= 0)
    {
      start_y -= (p_height + font_height);
    }
  }
  return {start_x, start_y, popup_rect.width(), popup_rect.height()};
}

void EditorArea::draw_popup_menu()
{
  QRect pum_rect = popupmenu_rect();
  auto start_x = pum_rect.x();
  auto start_y = pum_rect.y();
  popup_menu.move(start_x, start_y);
  popup_menu.setVisible(true);
  QPoint info_pos = {start_x + popup_menu.width(), start_y};
  auto& pum_info_widget = popup_menu.info_display();
  if (!pum_info_widget.isHidden()) pum_info_widget.move(info_pos);
}

static bool is_image(const QString& file)
{
  return !QImage(file).isNull();
}

void EditorArea::dropEvent(QDropEvent* event)
{
  const QMimeData* mime_data = event->mimeData();
  if (mime_data->hasUrls())
  {
    for(const auto& url : mime_data->urls())
    {
      if (!url.isLocalFile()) continue;
      if (is_image(url.toLocalFile()))
      {
        // Do something special for images?
      }
      std::string cmd = fmt::format("e {}", url.toLocalFile().toStdString());
      nvim->command(cmd);
    }
  }
  event->acceptProposedAction();
}

void EditorArea::dragEnterEvent(QDragEnterEvent* event)
{
  if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void EditorArea::set_fallback_for_ucs(u32 ucs)
{
  for(u32 i = 0; i < fonts.size(); ++i)
  {
    if (fonts[i].raw().supportsCharacter(ucs))
    {
      font_for_unicode[ucs] = i;
      return;
    }
  }
  font_for_unicode[ucs] = 0;
}

u32 EditorArea::font_for_ucs(u32 ucs)
{
  if (fonts.size() <= 1) return 0;
  auto it = font_for_unicode.find(ucs);
  if (it != font_for_unicode.end()) return it->second;
  set_fallback_for_ucs(ucs);
  return font_for_unicode.at(ucs);
}

void EditorArea::resizeEvent(QResizeEvent* event)
{
  Q_UNUSED(event);
  repaint();
}

/// Returns a point clamped between the top-left and bottom-right
/// of r.
static QPoint clamped(QPoint p, const QRect& r)
{
  p.setX(std::clamp(p.x(), r.left(), r.right()));
  p.setY(std::clamp(p.y(), r.top(), r.bottom()));
  return p;
}

static QRect scaled(const QRect& r, float hor, float vert)
{
  return QRectF(
    r.x() * hor,
    r.y() * vert,
    r.width() * hor,
    r.height() * vert
  ).toRect();
}

std::optional<EditorArea::GridPos>
EditorArea::grid_pos_for(QPoint pos)
{
  const auto [f_width, f_height] = font_dimensions();
  for(auto it = grids.rbegin(); it != grids.rend(); ++it)
  {
    const auto& grid = *it;
    QRect grid_rect = {grid->x, grid->y, grid->cols, grid->rows};
    QRect grid_px_rect = scaled(grid_rect, f_width, f_height);
    if (grid_px_rect.contains(pos))
    {
      int row = (pos.y() / f_height) - grid->y;
      int col = (pos.x() / f_width) - grid->x;
      auto x = clamped({col, row}, {0, 0, grid->cols, grid->rows});
      return GridPos {grid->id, x.y(), x.x()};
    }
  }
  return std::nullopt;
}

void EditorArea::send_mouse_input(
  QPoint pos,
  std::string button,
  std::string action,
  std::string modifiers
)
{
  if (!mouse_enabled) return;
  auto grid_pos_opt = grid_pos_for(pos);
  if (!grid_pos_opt)
  {
    fmt::print(
      "No grid found for action '{}' on ({},{})\n", action,
      pos.x(), pos.y()
    );
    return;
  }
  auto&& [grid_num, row, col] = *grid_pos_opt;
  if (!capabilities.multigrid) grid_num = 0;
  if (action == "press") mouse.gridid = grid_num;
  nvim->input_mouse(
    std::move(button), std::move(action), std::move(modifiers),
    grid_num, row, col
  );
}

void EditorArea::mousePressEvent(QMouseEvent* event)
{
  if (hide_cursor_while_typing && cursor() == Qt::BlankCursor)
  {
    unsetCursor();
  }
  if (cursor() != Qt::ArrowCursor)
  {
    QWidget::mousePressEvent(event);
    return;
  }
  if (!mouse_enabled) return;
  mouse.button_clicked(event->button());
  std::string btn_text = mouse_button_to_string(event->button());
  if (btn_text.empty()) return;
  std::string action = "press";
  std::string mods = mouse_mods_to_string(event->modifiers(), mouse.click_count);
  send_mouse_input(
    event->pos(), std::move(btn_text), std::move(action), std::move(mods)
  );
}

void EditorArea::mouseMoveEvent(QMouseEvent* event)
{
  if (hide_cursor_while_typing && cursor() == Qt::BlankCursor)
  {
    unsetCursor();
  }
  QWidget::mouseMoveEvent(event);
  if (cursor() != Qt::ArrowCursor) return;
  if (!mouse_enabled) return;
  auto button = mouse_button_to_string(event->buttons());
  if (button.empty()) return;
  auto mods = mouse_mods_to_string(event->modifiers());
  std::string action = "drag";
  const auto [f_width, f_height] = font_dimensions();
  QPoint text_pos(event->x() / f_width, event->y() / f_height);
  int grid_num = 0;
  if (mouse.gridid)
  {
    auto* grid = find_grid(mouse.gridid);
    if (grid && grid->is_float())
    {
      text_pos.rx() -= grid->x;
      text_pos.ry() -= grid->y;
      text_pos = clamped(text_pos, {0, 0, grid->cols, grid->rows});
    }
    grid_num = mouse.gridid;
  }
  // Check if mouse actually moved
  if (mouse.col == text_pos.x() && mouse.row == text_pos.y()) return;
  mouse.col = text_pos.x();
  mouse.row = text_pos.y();
  nvim->input_mouse(
    button, action, mods, grid_num, mouse.row, mouse.col
  );
}

void EditorArea::mouseReleaseEvent(QMouseEvent* event)
{
  if (!mouse_enabled) return;
  auto button = mouse_button_to_string(event->button());
  if (button.empty()) return;
  auto mods = mouse_mods_to_string(event->modifiers());
  std::string action = "release";
  mouse.gridid = 0;
  send_mouse_input(
    event->pos(), std::move(button), std::move(action), std::move(mods)
  );
}

void EditorArea::wheelEvent(QWheelEvent* event)
{
  if (!mouse_enabled) return;
  std::string button = "wheel";
  std::string action = "";
  auto mods = mouse_mods_to_string(event->modifiers());
  if (event->angleDelta().y() > 0) /* scroll up */ action = "up";
  else if (event->angleDelta().y() < 0) /* scroll down */ action = "down";
  if (action.empty()) return;
  QPoint wheel_pos = event->position().toPoint();
  send_mouse_input(
    wheel_pos, std::move(button), std::move(action), std::move(mods)
  );
}

void EditorArea::destroy_grid(u16 grid_num)
{
  auto it = std::find_if(grids.begin(), grids.end(), [=](const auto& g) {
    return g->id == grid_num;
  });
  if (it == grids.end()) return;
  grids.erase(it);
}

double EditorArea::font_offset() const
{
  return get_offset(font, linespace);
}

void EditorArea::send_redraw()
{
  //clear_events();
  //events.push({PaintKind::Redraw, 0, QRect()});
  for(auto& grid : grids) grid->send_redraw();
}

void EditorArea::send_clear(std::uint16_t grid_num, QRect r)
{
  Q_UNUSED(r);
  if (GridBase* grid = find_grid(grid_num); grid) grid->send_clear();
  //if (r.isNull())
  //{
    //GridBase* grid = find_grid(grid_num);
    //r = {grid->x, grid->y, grid->cols, grid->rows};
  //}
  //events.push({PaintKind::Clear, grid_num, r});
}

void EditorArea::send_draw(std::uint16_t grid_num, QRect r)
{
  if (GridBase* grid = find_grid(grid_num); grid) grid->send_draw(r);
  //events.push({PaintKind::Draw, grid_num, std::move(r)}); 
}

void EditorArea::inputMethodEvent(QInputMethodEvent* event)
{
  event->accept();
  if (!event->commitString().isEmpty())
  {
    nvim->send_input(event->commitString().toStdString());
  }
}

QVariant EditorArea::inputMethodQuery(Qt::InputMethodQuery query) const
{
  switch(query)
  {
    case Qt::ImFont:
      return font;
    case Qt::ImCursorRectangle:
    {
      auto&& [font_width, font_height] = font_dimensions();
      auto rect_opt = neovim_cursor.rect(font_width, font_height);
      if (!rect_opt) return QVariant();
      auto cr = *rect_opt;
      return cr.rect;
    }
    default:
      break;
  }
  return QVariant();
}
