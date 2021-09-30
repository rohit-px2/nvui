#ifndef NVUI_CMDLINE_HPP
#define NVUI_CMDLINE_HPP
#include <QGraphicsDropShadowEffect>
#include <QObject>
#include <QWidget>
#include <QPixmap>
#include <msgpack.hpp>
#include <optional>
#include "hlstate.hpp"
#include "cursor.hpp"

class CmdLine : public QWidget
{
public:
  CmdLine(const HLState* hl_state, Cursor* cursor, QWidget* parent = nullptr);
  /// Neovim event handlers
  void cmdline_show(std::span<Object> objs);
  void cmdline_hide(std::span<Object> objs);
  void cmdline_cursor_pos(std::span<Object> objs);
  void cmdline_special_char(std::span<Object> objs);
  void cmdline_block_show(std::span<Object> objs);
  void cmdline_block_append(std::span<Object> objs);
  void cmdline_block_hide(std::span<Object> objs);
  /**
   * Returns where the popup menu should be positioned.
   */
  QPoint popupmenu_pt(int pum_height, const QSize& window_sz) const
  {
    const int win_height = window_sz.height();
    // Where the top-left corner of the popup menu is to be drawn
    QPoint start_point {pos().x(), pos().y() + height()};
    // Show the popup menu above the cmdline if the conditions are met.
    if (pos().y() + height() + pum_height > win_height
        && (pos().y() - pum_height) >= 0)
    {
      start_point.setY(pos().y() - pum_height);
    }
    return start_point;
  }

  inline void set_border_color(QColor color) { border_color = color; }
  inline void set_border_width(float width) { border_width = width; }
  inline void set_font_family(const QString& new_family)
  {
    font.setFamily(new_family);
    big_font.setFamily(new_family);
  }
  
  inline void size_changed(const QSize& new_size)
  {
    QRect new_rect = get_rect_for(new_size);
    resize(new_rect.size());
    update();
  }

  void font_changed(const QFont& new_font);

  inline void set_font_size(const float new_font_size)
  {
    font_size = new_font_size;
    font.setPointSizeF(new_font_size);
    big_font.setPointSizeF(new_font_size * big_font_scale_ratio);
    update_metrics();
  }

  void update_metrics();

  inline void set_big_font_scale_ratio(float scale_ratio)
  {
    big_font_scale_ratio = scale_ratio;
    big_font.setPointSizeF(font_size * scale_ratio);
    update_metrics();
    resize(get_rect_for(qobject_cast<QWidget*>(parent())->size()).size());
  }

  QPoint get_top_left_for(const QSize& size) const noexcept
  {
    int start_x = rel_rect.x() * size.width();
    int start_y = rel_rect.y()  * size.height();
    return {start_x, start_y};
  }
  
  // Returns the height of the inner rectangle that is needed to fit all of the text that
  // is being displayed at the moment. If there is no text it still returns the base height.
  int fitting_height();

  int padded(int dim)
  {
    return dim + ((padding + border_width) * 2);
  }

  QRect inner_rect() const
  {
    QRect r = rect();
    int pad = padding + border_width;
    r.adjust(pad, pad, -pad, -pad);
    return r;
  }

  QRect get_rect_for(const QSize& size) const noexcept
  {
    int start_x = rel_rect.x() * size.width();
    int start_y = rel_rect.y()  * size.height();
    int end_x = rel_rect.right() * size.width();
    int end_y = rel_rect.bottom() * size.height();
    return {QPoint(start_x, start_y), QPoint(end_x, end_y)};
  }

  QPointF relative_pos() const { return {rel_rect.x(), rel_rect.y()}; }
  
  auto get_parent()
  {
    return qobject_cast<QWidget*>(parent());
  }

  void parent_resized(QSize new_size)
  {
    resize(get_rect_for(new_size).size());
  }

  inline void set_fg(QColor fg) { inner_fg = fg; }
  inline void set_bg(QColor bg) { inner_bg = bg; }
  inline void set_border_width(int width) { border_width = width; }
  inline void set_padding(int new_padding)
  {
    padding = new_padding;
    resize(get_rect_for(get_parent()->size()).size());
  }

  inline void set_x(float x)
  {
    if (x < 0.f || x > 1.0f) return;
    rel_rect.moveTo({x, rel_rect.y()});
    centered_x.reset();
  }

  inline void set_y(float y)
  {
    if (y < 0.f || y > 1.0f) return;
    rel_rect.moveTo({rel_rect.x(), y});
    centered_y.reset();
  }

  inline void set_width(float width)
  {
    rel_rect.setWidth(width);
    if (centered_x) { set_center_x(centered_x.value()); }
    resize(get_rect_for(get_parent()->size()).size());
  }

  inline void set_height(int height)
  {
    rel_rect.setHeight(height);
    if (centered_y) { set_center_y(centered_y.value()); }
    resize(get_rect_for(get_parent()->size()).size());
  }

  inline void set_center_x(float center_x)
  {
    float cur_width = rel_rect.width();
    rel_rect.setX(center_x - (cur_width / 2.f));
    rel_rect.setRight(center_x + (cur_width / 2.f));
    centered_x = center_x;
  }

  inline void set_center_y(float center_y)
  {
    float cur_height = rel_rect.height();
    rel_rect.setY(center_y - (cur_height / 2.f));
    rel_rect.setBottom(center_y + (cur_height / 2.f));
    centered_y = center_y;
  }
  
private:
  using line = std::vector<std::pair<QString, int>>;
  void draw_text_and_bg(
    QPainter& painter,
    const QString& text,
    const HLAttr& attr,
    const HLAttr& def_clrs,
    const QPointF& start,
    const QPointF& end,
    const int offset
  );
  std::optional<float> centered_x;
  std::optional<float> centered_y;
  // Parse and add new lines from new_line to line_arr.
  void add_line(ObjectArray& new_line);
  const HLState* state = nullptr;
  // Owned by EditorArea, but so is the cmdline
  // so there should be no problems
  Cursor* nvim_cursor = nullptr;
  // Relative measures for the inner rectangle of the cmdline.
  // (Where text is drawn).
  QRectF rel_rect {0.25, 0, 0.5, 0.10};
  // Border of the command line, and its color
  float border_width = 1.f;
  QColor border_color = "black";
  // Color of the inner part of the command line
  // (background and foreground)
  std::optional<QColor> inner_fg;
  std::optional<QColor> inner_bg;
  QPixmap pixmap {1, 1};
  std::optional<QString> first_char;
  std::vector<line> lines;
  std::vector<line> block_lines;
  // The command line contains its own, independent font size.
  // Not attached to guifont.
  // Neither is the command line font family.
  float font_size = 14.f;
  float font_width = 0.f;
  float font_height = 0.f;
  QFont font;
  std::uint32_t padding = 1;
  // How much bigger should the "big_font"
  // be than the regular size font? Default: 2x bigger
  float big_font_scale_ratio = 2.f;
  QFont big_font;
  float big_font_height = 0.f;
  float big_font_width = 0.f;
  QGraphicsDropShadowEffect shadow_effect;
  QFontMetricsF reg_metrics;
  QFontMetricsF big_metrics;
  std::optional<int> cursor_pos;
protected:
  void paintEvent(QPaintEvent* event) override;
  Q_OBJECT
};

#endif
