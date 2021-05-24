#include "editor.hpp"
#include "msgpack_overrides.hpp"
// QtCore private
#include <limits>
#include <private/qstringiterator_p.h>
#include <QApplication>
#include <QDebug>
#include <QDesktopWidget>
#include <algorithm>
#include <iostream>
#include <locale>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>

EditorArea::EditorArea(QWidget* parent, const HLState* hl_state, Nvim* nv)
: QWidget(parent),
  state(hl_state),
  nvim(nv)
{
  font.setPixelSize(15);
  update_font_metrics();
}

// uint32_t is enough to represent any code point
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
    grid.area[row * grid.cols + col + i] = GridChar {hl_id, std::move(c), is_dbl_width};
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

/// We don't do any actual painting here, we save that for the flush event.
/// Instead we save the data to an internal buffer.
void EditorArea::grid_line(const msgpack::object* obj, std::uint32_t size)
{
  std::stringstream ss;
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
    ss << '\n';
    // Update the area that we modified
    // Translating rows and cols to a pixel area
    //QRect rect = to_pixels(grid_num, start_row, start_col, start_row + 1, col);
  }
  //std::cout << ss.str() << '\n';
}

void EditorArea::grid_cursor_goto(const msgpack::object* obj, std::uint32_t size)
{
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
      font.setHintingPreference(QFont::HintingPreference::PreferVerticalHinting);
    }
  }
}

/// This is when we push the internal buffer to the window
void EditorArea::flush()
{
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
    const auto opts = parse_guifont(lst.at(0));
    const QString& font_name = std::get<0>(opts);
    const double font_size = std::get<1>(opts);
    const std::uint8_t font_opts = std::get<2>(opts);
    // Font needs to be valid
    if (!font_db.families().contains(font_name))
    {
      return;
    }
    else
    {
      font.setFamily(font_name);
    }

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
  const std::uint16_t grid_num,
  const std::uint16_t start_row,
  const std::uint16_t start_col,
  const std::uint16_t end_row,
  const std::uint16_t end_col
)
{
  const Grid* g = find_grid(grid_num);
  assert(g);
  const Grid& grid = *g;
  // grid's x and y are the (x, y), rows is the height, cols is the width
  // Multiply horizontal values by width, verticals by height, hopefully this works
  const QPoint start = {(grid.x + start_col) * font_width, (grid.y + start_row) * font_height};
  return {
    (grid.x + start_col) * font_width,
    (grid.y + start_row) * font_height,
    (end_col - start_col) * font_width,
    (end_row - start_row) * font_height
  };
}

void EditorArea::update_font_metrics()
{
  QFontMetricsF metrics {font};
  font_height = metrics.ascent() + metrics.descent() + linespace;
  // NOTE: This will only work for monospace fonts since we're basing every char's
  // spocing off a single char.
  constexpr QChar any_char = 'a';
  font_width = metrics.horizontalAdvance(any_char) + charspace;
}

QSize EditorArea::to_rc(const QSize& pixel_size)
{
  return {pixel_size.width() / font_width, pixel_size.height() / font_height};
}

void EditorArea::paintEvent(QPaintEvent* event)
{
  std::cout << "Paint event called\n";
}

std::tuple<std::uint16_t, std::uint16_t> EditorArea::font_dimensions() const
{
  return std::make_tuple(font_width, font_height);
}

void EditorArea::resized(QSize size)
{
  std::cout << "Done resizing?\n";
  const QSize new_rc = to_rc(size);
  assert(nvim);
  nvim->resize(new_rc.width(), new_rc.height());
}
