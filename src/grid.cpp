#include "grid.hpp"
#include "editor.hpp"

GridBase::GridBase(
  EditorArea* parent,
  u16 x,
  u16 y,
  u16 w,
  u16 h,
  u16 id
) : QWidget(parent),
    x(x),
    y(y),
    cols(w),
    rows(h),
    id(id),
    area(w * h),
    editor_area(parent)
{
}

GridBase::GridBase(const GridBase& other)
: QWidget(other.parentWidget()),
  x(other.x), y(other.y), cols(other.cols), rows(other.rows),
  id(other.id), area(other.area), hidden(other.hidden),
  editor_area(other.editor_area)
{
}

void GridBase::set_text(
  grid_char c,
  std::uint16_t row,
  std::uint16_t col,
  std::uint16_t hl_id,
  std::uint16_t repeat,
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
  //std::cout << "Set " << repeat << " texts at (" << row << ", " << col << ").\n";
  // Neovim should make sure this isn't out-of-bounds
  assert(col + repeat <= cols);
  for(std::uint16_t i = 0; i < repeat; ++i)
  {
    // row * cols - get current row
    assert(row * cols + col + i < area.size());
    area[row * cols + col + i] = {hl_id, c, is_dbl_width, ucs};
  }
}
