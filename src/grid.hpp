#ifndef NVUI_GRID_HPP
#define NVUI_GRID_HPP

#include <QStaticText>
#include <QString>
#include <QTimer>
#include <QWidget>
#include <cmath>
#include <queue>
#include "hlstate.hpp"
#include "utils.hpp"

using grid_char = QString;

class EditorArea;

struct GridChar
{
  int hl_id; // Shouldn't have more than 65k highlight attributes
  grid_char text;
  bool double_width = false;
  std::uint32_t ucs;
};

// Differentiate between redrawing and clearing (since clearing is
// a lot easier)
enum PaintKind : std::uint8_t
{
  Clear,
  Draw,
  Redraw
};

struct PaintEventItem
{
  PaintKind type;
  std::uint16_t grid_num;
  QRect rect;
};

struct Viewport
{
  std::uint32_t topline;
  std::uint32_t botline;
  std::uint32_t curline;
  std::uint32_t curcol;
};

// Default delete does nothing (variables clean up themselves)
template<typename T>
struct do_nothing_deleter
{
  void operator()(T* p) const { Q_UNUSED(p); }
};

/// LRUCache with optional custom deleter
/// I use the custom deleter for IDWriteTextLayout1
/// so they get automatically released
/// The caching strategy for QPaintGrid (using QStaticText)
/// cleans up itself so the deleter doesn't need to be specified
/// Once again, using Neovide's idea of caching text blobs.
/// See
/// https://github.com/neovide/neovide/blob/main/src/renderer/fonts/caching_shaper.rs
/// Uses QHash internally since it's expected to be used with QString
/// to cache drawn text.
template<typename K, typename V, typename ValueDeleter = do_nothing_deleter<V>>
class LRUCache
{
public:
  using key_type = K;
  using value_type = V;
  LRUCache(std::size_t capacity)
    : max_size(capacity),
      map(),
      keys()
  {
    map.reserve(static_cast<int>(max_size));
  }

  V* put(K k, V v)
  {
    V* ptr = nullptr;
    auto it = map.find(k);
    if (it != map.end())
    {
      it->first = std::move(v);
      ptr = &it->first;
      move_to_front(it->second);
    }
    else
    {
      keys.push_front(k);
      auto& pair_ref = map[k];
      pair_ref = {v, keys.begin()};
      ptr = &pair_ref.first;
    }
    if (keys.size() > max_size)
    {
      const auto& back_key = keys.back();
      auto erase_it = map.find(back_key);
      ValueDeleter()(std::addressof(erase_it->first));
      map.erase(erase_it);
      keys.pop_back();
    }
    return ptr;
  }

  const V* get(const K& k)
  {
    auto it = map.find(k);
    if (it != map.end())
    {
      move_to_front(it->second);
      return std::addressof(it->first);
    }
    else
    {
      return nullptr;
    }
  }

  ~LRUCache()
  {
    constexpr auto deleter = ValueDeleter();
    for(auto& val : map) deleter(std::addressof(val.first));
  }

  void clear()
  {
    constexpr auto deleter = ValueDeleter();
    for(auto& val : map) deleter(std::addressof(val.first));
    map.clear();
    keys.clear();
  }

private:
  // https://stackoverflow.com/questions/14579957/std-container-c-move-to-front
  void move_to_front(typename std::list<K>::iterator it)
  {
    if (it != keys.begin()) keys.splice(keys.begin(), keys, it);
  }
  std::size_t max_size = 10;
  QHash<K, QPair<V, typename std::list<K>::iterator>> map;
  std::list<K> keys;
};

namespace scalers
{
  // Follows the same idea
  // as Neovide's "ease" functions. time is between 0 - 1,
  // we can use some exponents to change the delta time so that
  // transition speed is not the same throughout.
  using time_scaler = float (*)(float);
  inline float oneminusexpo2negative10(float t)
  {
    // Taken from Neovide's "animation_utils.rs",
    // (specifically the "ease_out_expo" function).
    return 1.0f - std::pow(2.0, -10.0f * t);
  }
  inline float cube(float t)
  {
    return t * t * t;
  }
  inline float accel_continuous(float t)
  {
    return t * t * t * t;
  }
  inline float fast_start(float t)
  {
    return std::pow(t, 1.0/9.0);
  }
  inline float quadratic(float t)
  {
    return t * t;
  }
  inline float identity(float t)
  {
    return t;
  }
  /// Update this when a new scaler is added.
  inline const std::unordered_map<std::string, time_scaler>&
  scalers()
  {
    static const std::unordered_map<std::string, time_scaler> scaler_map {
      {"expo", oneminusexpo2negative10},
      {"cube", cube},
      {"fourth", accel_continuous},
      {"fast_start", fast_start},
      {"quad", quadratic},
      {"identity", identity}
    };
    return scaler_map;
  }
}

/// The base grid object, no rendering functionality.
/// Contains some convenience functions for setting text,
/// position, size, etc.
/// The event queue contains the rendering commands to
/// be performed.
class GridBase : public QObject
{
  Q_OBJECT
public:
  using u16 = std::uint16_t;
  using u32 = std::uint32_t;
  GridBase(
    u16 x,
    u16 y,
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
  GridBase(const GridBase& other)
  : QObject{}, x(other.x), y(other.y), cols(other.cols), rows(other.rows),
    id(other.id), area(other.area), hidden(other.hidden),
    viewport(other.viewport)
  {
  }
  GridBase& operator=(const GridBase& other)
  {
    x = other.x;
    y = other.y;
    cols = other.cols;
    rows = other.rows;
    id = other.id;
    area = other.area;
    hidden = other.hidden;
    viewport = other.viewport;
    return *this;
  }
  virtual ~GridBase() = default;
  void set_text(
    grid_char c,
    u16 row,
    u16 col,
    u16 hl_id,
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
  }

  /**
   * Set the size of the grid (cols x rows)
   * to width x height.
   */
  virtual void set_size(u16 w, u16 h)
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
  virtual void set_pos(u16 new_x, u16 new_y)
  {
    x = new_x;
    y = new_y;
  }
  void set_pos(QPoint p) { set_pos(p.x(), p.y()); }
  /// Send a redraw message to the grid
  void send_redraw()
  {
    clear_event_queue();
    evt_q.push({PaintKind::Redraw, 0, QRect()});
  }
  void send_clear()
  {
    clear_event_queue();
    evt_q.push({PaintKind::Clear, 0, QRect()});
  }
  void send_draw(QRect r)
  {
    evt_q.push({PaintKind::Draw, 0, r});
  }
  /// Grid's top left position
  QPoint top_left() { return {x, y}; };
  QPoint bot_right() { return {x + cols, y + rows}; }
  /// Grid's bottom right position
  QPoint bot_left() { return {x, y + rows}; }
  QPoint top_right() { return { x + cols, y}; }
  /// Clear the event queue
  void clear_event_queue()
  {
    decltype(evt_q)().swap(evt_q);
  }
  /// Change the viewport to the new viewport.
  virtual void viewport_changed(Viewport vp)
  {
    viewport = vp;
  }
public:
  u16 x;
  u16 y;
  u16 cols;
  u16 rows;
  u16 id;
  std::size_t z_index = 0;
  std::vector<GridChar> area; // Size = rows * cols
  bool hidden = false;
  std::queue<PaintEventItem> evt_q;
  Viewport viewport;
  /// Not used in GridBase (may not even be used at all
  /// if animations are not supported/enabled by the rendering
  /// grid).
  static scalers::time_scaler scroll_scaler;
  static scalers::time_scaler move_scaler;
};

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
    const int offset,
    QFont font
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

#endif // NVUI_GRID_HPP
