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
  const std::size_t max_iterations
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

void EditorArea::grid_resize(std::span<NeovimObj> objs)
{
  // Should only run once
  for(const auto& obj : objs)
  {
    auto vars = obj.try_decompose<u64, u64, u64>();
    if (!vars) continue;
    auto [grid_num, width, height] = *vars;
    assert(grid_num != 0);
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
      move_to_top(grid);
    }
    if (grid)
    {
      send_draw(grid_num, {0, 0, grid->cols, grid->rows});
    }
  }
}

void EditorArea::grid_line(std::span<NeovimObj> objs)
{
  int hl_id = 0;
  for(auto& grid_cmd : objs)
  {
    auto* arr = grid_cmd.array();
    assert(arr && arr->size() >= 4);
    const auto grid_num = arr->at(0).get<u64>();
    GridBase* grid_ptr = find_grid(grid_num);
    if (!grid_ptr) continue;
    if (!grid_ptr) return;
    GridBase& g = *grid_ptr;
    int start_row = (int) arr->at(1);
    int start_col = (int) arr->at(2);
    int col = start_col;
    auto cells = arr->at(3).array();
    assert(cells);
    for(auto& cell : *cells)
    {
      assert(cell.has<ObjectArray>());
      auto& cell_arr = cell.get<ObjectArray>();
      assert(cell_arr.size() >= 1 && cell_arr.size() <= 3);
      // [text, (hl_id, repeat)]
      int repeat = 1;
      assert(cell_arr.at(0).is_string());
      grid_char text = GridChar::grid_char_from_str(cell_arr[0].get<std::string>());
      // If the previous char was a double-width char,
      // the current char is an empty string.
      bool prev_was_dbl = text.isEmpty();
      bool is_dbl = false;
      if (prev_was_dbl)
      {
        std::size_t idx = start_row * grid_ptr->cols + col - 1;
        if (idx < grid_ptr->area.size())
        {
          grid_ptr->area[idx].double_width = true;
        }
      }
      switch(cell_arr.size())
      {
        case 2:
          hl_id = (int) cell_arr.at(1);
          break;
        case 3:
          hl_id = (int) cell_arr.at(1);
          repeat = (int) cell_arr.at(2);
          break;
      }
      g.set_text(std::move(text), start_row, col, hl_id, repeat, is_dbl);
      col += repeat;
    }
    send_draw(grid_num, {start_col, start_row, (col - start_col), 1});
  }
}

void EditorArea::grid_cursor_goto(std::span<NeovimObj> objs)
{
  if (objs.empty()) return;
  const auto& obj = objs.back();
  auto vars = obj.try_decompose<u16, int, int>();
  if (!vars) return;
  auto [grid_num, row, col] = *vars;
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

void EditorArea::option_set(std::span<NeovimObj> objs)
{
  static const auto extension_for = [&](const auto& s) -> bool* {
    if (s == "ext_linegrid") return &capabilities.linegrid;
    else if (s == "ext_popupmenu") return &capabilities.popupmenu;
    else if (s == "ext_cmdline") return &capabilities.cmdline;
    else if (s == "ext_multigrid") return &capabilities.multigrid;
    else if (s == "ext_wildmenu") return &capabilities.wildmenu;
    else if (s == "ext_messages") return &capabilities.messages;
    else return nullptr;
  };
  for(const auto& obj : objs)
  {
    auto* arr = obj.array();
    assert(arr && arr->size() >= 2);
    auto* opt_ptr = arr->at(0).string();
    assert(opt_ptr);
    auto& opt = *opt_ptr;
    if (bool* capability = extension_for(opt); capability)
    {
      auto* bool_ptr = arr->at(1).boolean();
      assert(bool_ptr);
      *capability = *bool_ptr;
    }
    else if (opt == "guifont")
    {
      set_guifont(QString::fromStdString(arr->at(1).get<std::string>()));
      font.setHintingPreference(QFont::PreferFullHinting);
    }
    else if (opt == "linespace")
    {
      auto int_opt = arr->at(1).try_convert<int>();
      assert(int_opt);
      linespace = *int_opt;
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

void EditorArea::win_pos(std::span<NeovimObj> objs)
{
  for(const auto& obj : objs)
  {
    auto vars = obj.try_decompose<u64, NeovimExt, u64, u64, u64, u64>();
    if (!vars) continue;
    const auto& [grid_num, win, sr, sc, width, height] = *vars;
    GridBase* grid = find_grid(grid_num);
    if (!grid)
    {
      fmt::print("No grid #{} found.\n", grid_num);
      continue;
    }
    grid->hidden = false;
    grid->win_pos(sc, sr);
    grid->set_size(width, height);
    move_to_top(grid);
    grid->winid = get_win(win);
  }
  send_redraw();
}

void EditorArea::win_hide(std::span<NeovimObj> objs)
{
  for(const auto& obj : objs)
  {
    auto* arr = obj.array();
    assert(arr && arr->size() >= 2);
    auto grid_num = arr->at(0).u64();
    if (!grid_num) continue;
    if (GridBase* grid = find_grid(*grid_num)) grid->hidden = true;
  }
}

void EditorArea::win_float_pos(std::span<NeovimObj> objs)
{
  for(const auto& obj : objs)
  {
    auto vars = obj.try_decompose<u64, NeovimExt, std::string, u64, double, double>();
    if (!vars) continue;
    auto [grid_num, win, anchor_dir, anchor_grid_num, anchor_row, anchor_col] = *vars;
    int zindex = -1;
    if (auto* params = obj.array(); params && params->size() >= 7)
    {
      zindex = params->at(6).try_convert<int>().value_or(-1);
    }
    QPointF anchor_rel(anchor_col, anchor_row);
    //bool focusable = arr.ptr[6].as<bool>();
    GridBase* grid = find_grid(grid_num);
    GridBase* anchor_grid = find_grid(anchor_grid_num);
    if (!grid || !anchor_grid) continue;
    // Anchor dir is "NW", "SW", "SE", "NE"
    // The anchor direction indicates which corner of the grid
    // should be at the position given.
    QPoint anchor_pos = anchor_grid->top_left() + anchor_rel.toPoint();
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
      const auto [font_w, font_h] = font_dimensions();
      const QPoint pum_tr = popupmenu_rect().topRight();
      // Don't let the grid get clipped by the popup menu
      const float pum_rx = std::round(pum_tr.x() / font_w);
      const float pum_ty = std::ceil(pum_tr.y() / font_h);
      anchor_pos = QPoint(pum_rx, pum_ty);
    }
    bool were_animations_enabled = animations_enabled();
    set_animations_enabled(false);
    grid->winid = get_win(win);
    move_to_top(grid);
    grid->float_pos(anchor_pos.x(), anchor_pos.y());
    QPointF absolute_win_pos = anchor_rel + QPointF {anchor_col, anchor_row};
    grid->set_float_ordering_info(zindex, absolute_win_pos);
    set_animations_enabled(were_animations_enabled);
  }
}

void EditorArea::win_close(std::span<NeovimObj> objs)
{
  for(const auto& obj : objs)
  {
    auto* arr = obj.array();
    if (!(arr && arr->size() >= 1)) continue;
    auto grid_num = arr->at(0).u64();
    if (!grid_num) continue;
    destroy_grid(*grid_num);
  }
  send_redraw();
}

void EditorArea::win_viewport(std::span<NeovimObj> objs)
{
  for(const auto& obj : objs)
  {
    auto vars = obj.try_decompose<u64, NeovimExt, u32, u32, u32, u32>();
    if (!vars) continue;
    auto [grid_num, ext, topline, botline, curline, curcol] = *vars;
    Viewport vp = {topline, botline, curline, curcol};
    auto* grid = find_grid(grid_num);
    if (!grid) continue;
    grid->viewport_changed(std::move(vp));
  }
}

void EditorArea::grid_destroy(std::span<NeovimObj> objs)
{
  for(const auto& obj : objs)
  {
    auto& arr = obj.get<ObjectArray>();
    assert(arr.size() >= 1);
    auto grid_num = arr.at(0).get<u64>();
    destroy_grid(grid_num);
  }
  send_redraw();
}

void EditorArea::msg_set_pos(std::span<NeovimObj> objs)
{
  for(const auto& obj : objs)
  {
    auto vars = obj.try_decompose<u64, u64>();
    if (!vars) continue;
    auto [grid_num, row] = *vars;
    //auto scrolled = arr.ptr[2].as<bool>();
    //auto sep_char = arr.ptr[3].as<QString>();
    if (GridBase* grid = find_grid(grid_num))
    {
      grid->set_pos(grid->x, row);
      move_to_top(grid);
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
      FontOptions font_opts = FontOpts::Normal;
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
  font::set_opts(font, font_opts);
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

FontDimensions EditorArea::font_dimensions() const
{
  return {font_width, font_height};
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

void EditorArea::grid_clear(std::span<NeovimObj> objs)
{
  for(const auto& obj : objs)
  {
    auto* arr = obj.array();
    assert(arr && arr->size() >= 1);
    const auto grid_num = arr->at(0).u64();
    assert(grid_num);
    auto* grid = find_grid(*grid_num);
    for(auto& gc : grid->area)
    {
      gc = {0, " ", false, QChar(' ').unicode()};
    }
    QRect&& r = {grid->x, grid->y, grid->cols, grid->rows};
    send_clear(*grid_num, r);
  }
}

void EditorArea::grid_scroll(std::span<NeovimObj> objs)
{
  for(const auto& obj : objs)
  {
    auto vars = obj.try_decompose<u16, u16, u16, u16, u16, int>();
    if (!vars) continue;
    const auto [grid_num, top, bot, left, right, rows] = *vars;
    // const int cols = arr->at(6);
    GridBase* grid = find_grid(grid_num);
    if (!grid) return;
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

void EditorArea::mode_info_set(std::span<NeovimObj> objs)
{
  neovim_cursor.mode_info_set(objs);
}

void EditorArea::mode_change(std::span<NeovimObj> objs)
{
  neovim_cursor.mode_change(objs);
}

void EditorArea::draw_cursor(QPainter& painter)
{
  auto grid_num = neovim_cursor.grid_num();
  if (grid_num < 0) return;
  if (auto* grid = static_cast<QPaintGrid*>(find_grid(grid_num)))
  {
    grid->draw_cursor(painter, neovim_cursor);
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
  if (fonts.size() <= 1 || ucs < 256) return 0;
  auto it = font_for_unicode.find(ucs);
  if (it != font_for_unicode.end()) return it->second;
  set_fallback_for_ucs(ucs);
  return font_for_unicode.at(ucs);
}

void EditorArea::resizeEvent(QResizeEvent* event)
{
  Q_UNUSED(event);
  update();
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
  if (action == "press")
  {
    mouse.gridid = grid_num;
    mouse.row = row;
    mouse.col = col;
  }
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
    if (grid)
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

void EditorArea::sort_grids_by_z_index()
{
  std::sort(grids.begin(), grids.end(), [](const auto& g1, const auto& g2) {
    return *g1 < *g2;
  });
  std::stable_partition(grids.begin(), grids.end(), [&](const auto& grid) {
    return !grid->is_float();
  });
}

std::int64_t EditorArea::get_win(const NeovimExt& ext)
{
  using namespace std;
  vector<uint8_t> data;
  for(const auto& c : ext.data) data.push_back(static_cast<uint8_t>(c));
  const auto size = ext.data.size();
  if (size == 1 && data[0] <= 0x7f) return (int) data[0];
  else if (size == 1 && data[0] >= 0xe0)
  {
    return int8_t(data[0]);
  }
  else if (size == 2 && data[0] == 0xcc) return (int) data[1];
  else if (size == 2 && data[0] == 0xd0) return int8_t(data[1]);
  else if (size == 3 && data[0] == 0xcd)
  {
    return u16(data[2]) | u16(data[1]) << 8;
  }
  else if (size == 3 && data[0] == 0xd1)
  {
    return int16_t(uint16_t(data[2])) | uint16_t(data[1]) << 8;
  }
  else if (size == 5 && data[0] == 0xce)
  {
    return u32(data[4] | u32(data[3]) << 8
         | u32(data[2]) << 16 | u32(data[1]) << 24);
  }
  return numeric_limits<int>::min();
}
