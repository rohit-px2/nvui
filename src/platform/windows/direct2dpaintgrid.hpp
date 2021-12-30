#ifndef NVUI_PLATFORM_WINDOWS_DIRECT2DPAINTGRID_HPP
#define NVUI_PLATFORM_WINDOWS_DIRECT2DPAINTGRID_HPP

#include <QString>
#include <QTimer>
#include <d2d1.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <dwrite_1.h>
#include "grid.hpp"
#include "hlstate.hpp"
#include "cursor.hpp"
#include "lru.hpp"
#include <wrl/client.h>

class WinEditorArea;
class D2DEditor;

/// Windows only class, uses Direct2D and DirectWrite.
/// This grid paints onto an ID2D1Bitmap object and works
/// with the WinEditorArea class to draw these bitmaps
/// to the screen (HWND).
class D2DPaintGrid : public GridBase
{
  Q_OBJECT
  struct Snapshot
  {
    Viewport vp;
    ID2D1Bitmap1* image;
  };
  template<typename Resource>
  struct WinDeleter
  {
    void operator()(Resource** res) const
    {
      if (*res)
      {
        (*res)->Release();
        *res = nullptr;
      }
    }
  };
  using TextLayoutDeleter = WinDeleter<IDWriteTextLayout1>;
public:
  using GridBase::u16;
  using GridBase::u32;
  using d2pt = D2D1_POINT_2F;
  using d2rect = D2D1_RECT_F;
  using d2color = D2D1::ColorF;
  using cache_type =
    LRUCache<QPair<QString, FontOptions>, IDWriteTextLayout1*, TextLayoutDeleter>;
public:
  template<typename... GridBaseArgs>
  D2DPaintGrid(WinEditorArea* wea, GridBaseArgs... args)
    : GridBase(args...),
      editor_area(wea),
      layout_cache(2000)
  {
    initialize_context();
    initialize_cache();
    update_bitmap_size();
    initialize_scroll_animation();
    initialize_move_animation();
  }
  ~D2DPaintGrid();
  ID2D1Bitmap1* buffer() { return bitmap; }
  /// Returns the position of the top-left corner of the grid.
  /// (Pixel position).
  d2pt pos() const;
  /// Returns the grid's rectangle position relative to the top-left
  /// corner of the editor area.
  d2rect rect() const;
  /// Returns the grid's source rectangle.
  d2rect source_rect() const;
  /// Process the event queue, painting the updates
  /// to the bitmap.
  void process_events();
  void set_size(u16 w, u16 h) override;
  void set_pos(u16 new_x, u16 new_y) override;
  void viewport_changed(Viewport vp) override;
  /// Update the top_left position of the grid. This is in terms
  /// of text so to get the pixel position you would need to multiply
  /// by the dimensions of the font being used. Doubles are used to
  /// allow for better pixel precision.
  void update_position(double x, double y);
  /// Render this grid to a render target
  void render(ID2D1RenderTarget* render_target);
  /// Draw the given cursor to the render target at the appropriate position
  void draw_cursor(ID2D1RenderTarget* target, const Cursor& cursor);
private:
  std::vector<Snapshot> snapshots;
  WinEditorArea* editor_area = nullptr;
  ID2D1Bitmap1* bitmap = nullptr;
  ID2D1DeviceContext* context = nullptr;
  QTimer move_update_timer {};
  float move_animation_time = -1.f; // Number of seconds till animation ends
  QPointF top_left = {0, 0};
  float start_scroll_y = 0.f;
  float current_scroll_y = 0.f;
  bool is_scrolling = false;
  float cur_left = 0.f;
  float cur_top = 0.f;
  float scroll_animation_time;
  QTimer scroll_animation_timer {};
  float dest_move_x = 0.f;
  float dest_move_y = 0.f;
  float old_move_x = 0.f;
  float old_move_y = 0.f;
  float dest_scroll_y = 0.f;
  using FontOptions = decltype(HLAttr::font_opts);
  /// A lot of time is spent text shaping, we cache the created text
  /// layouts
  cache_type layout_cache;
  /// Update the size of the bitmap to match the
  /// grid size
  void update_bitmap_size();
  /// Initialize the device contexts and bitmap.
  void initialize_context();
  /// Initialize the cache
  void initialize_cache();
  /// Initialize the move animation
  void initialize_move_animation();
  /// Initialize the scroll animation
  void initialize_scroll_animation();
  /// Draw the grid range given by the rect.
  /// Since we draw from the top-left, no offset is needed
  /// (unlike in QPaintGrid).
  void draw(
    ID2D1RenderTarget* context,
    QRect r,
    ID2D1SolidColorBrush* fg_brush,
    ID2D1SolidColorBrush* bg_brush
  );
  /// Draw text onto the given device context, clipped
  /// between start and end. Background & foreground colors
  /// are controlled by the main and fallback highlight attributes.
  void draw_text_and_bg(
    ID2D1RenderTarget* context,
    const QString& buf,
    const HLAttr& main,
    const HLAttr& fallback,
    D2D1_POINT_2F start,
    D2D1_POINT_2F end,
    float font_width,
    float font_height,
    IDWriteTextFormat* text_format,
    ID2D1SolidColorBrush* fg_brush,
    ID2D1SolidColorBrush* bg_brush
  );
  void draw_text(
    ID2D1RenderTarget& target,
    const QString& text,
    const Color& fg,
    const Color& sp,
    const FontOptions font_opts,
    D2D1_POINT_2F top_left,
    D2D1_POINT_2F bot_right,
    float font_width,
    float font_height,
    ID2D1SolidColorBrush& fg_brush,
    IDWriteTextFormat* text_format,
    bool clip = false
  );
  void draw_bg(
    ID2D1RenderTarget& target,
    const Color& bg,
    D2D1_POINT_2F top_left,
    D2D1_POINT_2F bot_right,
    ID2D1SolidColorBrush& brush
  );
  void draw_bg(
    ID2D1RenderTarget& target,
    const Color& bg,
    D2D1_RECT_F rect,
    ID2D1SolidColorBrush& brush
  );
  /// Returns a copy of src.
  /// NOTE: Must be released.
  ID2D1Bitmap1* copy_bitmap(ID2D1Bitmap1* src);
};

/// Windows only class, uses Direct2D and DirectWrite.
/// This grid paints onto an ID2D1Bitmap object and works
/// with the WinEditorArea class to draw these bitmaps
/// to the screen (HWND).
class D2DPaintGrid2 : public GridBase
{
  Q_OBJECT
  template<typename T>
  using ComPtr = Microsoft::WRL::ComPtr<T>;
  struct Snapshot
  {
    Viewport vp;
    ID2D1Bitmap* image;
  };
  template<typename Resource>
  struct WinDeleter
  {
    void operator()(Resource** res) const
    {
      if (*res)
      {
        (*res)->Release();
        *res = nullptr;
      }
    }
  };
  using TextLayoutDeleter = WinDeleter<IDWriteTextLayout1>;
public:
  using GridBase::u16;
  using GridBase::u32;
  using d2pt = D2D1_POINT_2F;
  using d2rect = D2D1_RECT_F;
  using d2color = D2D1::ColorF;
  using cache_type =
    LRUCache<QPair<QString, FontOptions>, IDWriteTextLayout1*, TextLayoutDeleter>;
public:
  template<typename... GridBaseArgs>
  D2DPaintGrid2(D2DEditor* wea, GridBaseArgs... args)
    : GridBase(args...),
      editor_area(wea),
      layout_cache(2000)
  {
    update_render_target();
    initialize_cache();
    initialize_scroll_animation();
    initialize_move_animation();
  }
  ~D2DPaintGrid2();
  /// Returns the position of the top-left corner of the grid.
  /// (Pixel position).
  d2pt pos() const;
  /// Returns the grid's rectangle position relative to the top-left
  /// corner of the editor area.
  d2rect rect() const;
  /// Returns the grid's source rectangle.
  d2rect source_rect() const;
  /// Process the event queue, painting the updates
  /// to the bitmap.
  void process_events();
  void set_size(u16 w, u16 h) override;
  void set_pos(u16 new_x, u16 new_y) override;
  void viewport_changed(Viewport vp) override;
  /// Update the top_left position of the grid. This is in terms
  /// of text so to get the pixel position you would need to multiply
  /// by the dimensions of the font being used. Doubles are used to
  /// allow for better pixel precision.
  void update_position(double x, double y);
  /// Render this grid to a render target
  void render(ID2D1RenderTarget* render_target);
  /// Draw the given cursor to the render target at the appropriate position
  void draw_cursor(ID2D1RenderTarget* target, const Cursor& cursor);
private:
  std::vector<Snapshot> snapshots;
  D2DEditor* editor_area = nullptr;
  ComPtr<ID2D1BitmapRenderTarget> render_target = nullptr;
  QTimer move_update_timer {};
  float move_animation_time = -1.f; // Number of seconds till animation ends
  QPointF top_left = {0, 0};
  float start_scroll_y = 0.f;
  float current_scroll_y = 0.f;
  bool is_scrolling = false;
  float cur_left = 0.f;
  float cur_top = 0.f;
  float scroll_animation_time;
  QTimer scroll_animation_timer {};
  float dest_move_x = 0.f;
  float dest_move_y = 0.f;
  float old_move_x = 0.f;
  float old_move_y = 0.f;
  float dest_scroll_y = 0.f;
  using FontOptions = decltype(HLAttr::font_opts);
  /// A lot of time is spent text shaping, we cache the created text
  /// layouts
  cache_type layout_cache;
  /// Update the size of the bitmap to match the
  /// grid size
  void update_render_target();
  /// Initialize the device contexts and bitmap.
  void initialize_context();
  /// Initialize the cache
  void initialize_cache();
  /// Initialize the move animation
  void initialize_move_animation();
  /// Initialize the scroll animation
  void initialize_scroll_animation();
  /// Draw the grid range given by the rect.
  /// Since we draw from the top-left, no offset is needed
  /// (unlike in QPaintGrid).
  void draw(
    ID2D1RenderTarget* context,
    QRect r,
    ID2D1SolidColorBrush* fg_brush,
    ID2D1SolidColorBrush* bg_brush
  );
  /// Draw text onto the given device context, clipped
  /// between start and end. Background & foreground colors
  /// are controlled by the main and fallback highlight attributes.
  void draw_text_and_bg(
    ID2D1RenderTarget* context,
    const QString& buf,
    const HLAttr& main,
    const HLAttr& fallback,
    D2D1_POINT_2F start,
    D2D1_POINT_2F end,
    float font_width,
    float font_height,
    IDWriteTextFormat* text_format,
    ID2D1SolidColorBrush* fg_brush,
    ID2D1SolidColorBrush* bg_brush
  );
  void draw_text(
    ID2D1RenderTarget& target,
    const QString& text,
    const Color& fg,
    const Color& sp,
    const FontOptions font_opts,
    D2D1_POINT_2F top_left,
    D2D1_POINT_2F bot_right,
    float font_width,
    float font_height,
    ID2D1SolidColorBrush& fg_brush,
    IDWriteTextFormat* text_format,
    bool clip = false
  );
  void draw_bg(
    ID2D1RenderTarget& target,
    const Color& bg,
    D2D1_POINT_2F top_left,
    D2D1_POINT_2F bot_right,
    ID2D1SolidColorBrush& brush
  );
  void draw_bg(
    ID2D1RenderTarget& target,
    const Color& bg,
    D2D1_RECT_F rect,
    ID2D1SolidColorBrush& brush
  );
  /// Returns a copy of src.
  /// NOTE: Must be released.
  ID2D1Bitmap* copy_bitmap(ID2D1Bitmap* src);
};

#endif // NVUI_PLATFORM_WINDOWS_DIRECT2DPAINTGRID_HPP
