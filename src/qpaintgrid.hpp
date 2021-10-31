#ifndef NVUI_QPAINTGRID_HPP
#define NVUI_QPAINTGRID_HPP

#include "grid.hpp"
#include "scalers.hpp"
#include "cursor.hpp"
#include "lru.hpp"

/// A class that implements rendering for a grid using Qt's
/// QPainter. The QPaintGrid class draws to a QPixmap and
/// works with the EditorArea class to draw these pixmaps to
/// the screen.
class QPaintGrid : public GridBase
{
  Q_OBJECT
  using GridBase::u16;
  struct Snapshot
  {
    Viewport vp;
    QPixmap image;
  };
public:
  template<typename... GridBaseArgs>
  QPaintGrid(EditorArea* ea, GridBaseArgs... args)
    : GridBase(args...),
      editor_area(ea),
      pixmap(),
      top_left(),
      text_cache(2000)
  {
    update_pixmap_size();
    update_position(x, y);
    initialize_cache();
    initialize_scroll_animation();
    initialize_move_animation();
  }
  ~QPaintGrid() override = default;
  void set_size(u16 w, u16 h) override;
  void set_pos(u16 new_x, u16 new_y) override;
  void viewport_changed(Viewport vp) override;
  /// Process the draw commands in the event queue
  void process_events();
  /// Returns the grid's paint buffer (QPixmap)
  const QPixmap& buffer() const { return pixmap; }
  /// The top-left corner of the grid (where to start drawing the buffer).
  QPointF pos() const { return top_left; }
  /// Renders to the painter.
  void render(QPainter& painter);
  /// Draws the cursor on the painter, relative to the grid's
  /// current position (see pos())
  void draw_cursor(QPainter& painter, const Cursor& cursor);
private:
  /// Draw the grid range given by the rect.
  void draw(QPainter& p, QRect r, const double font_offset);
  /// Draw the given text with attr and def_clrs indicating
  /// the background, foreground colors and font options.
  void draw_text_and_bg(
    QPainter& painter,
    const QString& text,
    const HLAttr& attr,
    const HLAttr& def_clrs,
    const QPointF& start,
    const QPointF& end,
    const double offset,
    QFont font,
    float font_width,
    float font_height
  );
  void draw_text(
    QPainter& painter,
    const QString& text,
    const Color& fg,
    const std::optional<Color>& sp,
    const QRectF& rect,
    const FontOptions font_opts,
    QFont& font,
    float font_width,
    float font_height,
    double offset = 0.
  );
  /// Update the pixmap size
  void update_pixmap_size();
  /// Initialize the cache
  void initialize_cache();
  /// Initialize scroll animation timer
  void initialize_scroll_animation();
  /// Initialize move animation timer
  void initialize_move_animation();
  /// Update the grid's position (new position can be found through pos()).
  void update_position(double new_x, double new_y);
private:
  std::vector<Snapshot> snapshots;
  /// Links up with the default Qt rendering
  EditorArea* editor_area;
  QPixmap pixmap;
  QTimer move_update_timer {};
  float move_animation_time = -1.f;
  QPointF top_left;
  float start_scroll_y = 0.f;
  float current_scroll_y = 0.f;
  float cur_left = 0.f;
  float cur_top = 0.f;
  bool is_scrolling = false;
  float scroll_animation_time;
  QTimer scroll_animation_timer {};
  float dest_move_x = 0.f;
  float dest_move_y = 0.f;
  float old_move_x = 0.f;
  float old_move_y = 0.f;
  float destination_scroll_y = 0.f;
  using FontOptions = decltype(HLAttr::font_opts);
  LRUCache<QPair<QString, FontOptions>, QStaticText> text_cache;
};

#endif // NVUI_QPAINTGRID_HPP
