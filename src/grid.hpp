#ifndef NVUI_GRID_HPP
#define NVUI_GRID_HPP

#include <QString>
#include <QWidget>

using grid_char = QString;

class EditorArea;

struct GridChar
{
  int hl_id; // Shouldn't have more than 65k highlight attributes
  grid_char text;
  bool double_width = false;
  std::uint32_t ucs;
};

class GridBase : public QWidget
{
  friend class QPaintGrid;
public:
  using u16 = std::uint16_t;
  GridBase(EditorArea* parent, u16 x, u16 y, u16 w, u16 h, u16 id);
  GridBase(const GridBase& other);

  void set_text(
    grid_char c,
    std::uint16_t row,
    std::uint16_t col,
    std::uint16_t hl_id,
    std::uint16_t repeat,
    bool is_dbl_width
  );

public:
  u16 x;
  u16 y;
  u16 cols;
  u16 rows;
  u16 id;
  std::vector<GridChar> area; // Size = rows * cols
  bool hidden = false;
  EditorArea* editor_area = nullptr;
  Q_OBJECT
};

class QPaintGrid : public GridBase
{
};

using Grid = GridBase;
#endif // NVUI_GRID_HPP
