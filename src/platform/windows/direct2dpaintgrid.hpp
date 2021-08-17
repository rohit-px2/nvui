#ifndef NVUI_PLATFORM_WINDOWS_DIRECT2DPAINTGRID_HPP
#define NVUI_PLATFORM_WINDOWS_DIRECT2DPAINTGRID_HPP

#include <QString>
#include <QTimer>
#include <d2d1.h>
#include <d2d1_1.h>
#include <d2d1_2.h>
#include <d2d1_3.h>
#include <dwrite.h>
#include <dwrite_1.h>
#include <dwrite_2.h>
#include <dwrite_3.h>
#include "grid.hpp"
#include "hlstate.hpp"

class WinEditorArea;

/// Windows only class, uses Direct2D and DirectWrite.
/// This grid paints onto an ID2D1Bitmap object and works
/// with the WinEditorArea class to draw these bitmaps
/// to the screen (HWND).
class D2DPaintGrid : public GridBase
{
public:
  using GridBase::u16;
  using GridBase::u32;
  using d2pt = D2D1_POINT_2F;
  using d2rect = D2D1_RECT_F;
  using d2color = D2D1::ColorF;
public:
  D2DPaintGrid(WinEditorArea* wea, auto... args)
    : GridBase(args...),
      editor_area(wea)
  {
    initialize_context();
    update_bitmap_size();
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
  /// Update the top_left position of the grid. This is in terms
  /// of text so to get the pixel position you would need to multiply
  /// by the dimensions of the font being used. Doubles are used to
  /// allow for better pixel precision.
  void update_position(double x, double y);
private:
  WinEditorArea* editor_area = nullptr;
  ID2D1Bitmap1* bitmap = nullptr;
  ID2D1DeviceContext* context = nullptr;
  QTimer move_update_timer {};
  float move_animation_time = -1.f; // Number of seconds till animation ends
  QPointF top_left = {0, 0};
  /// Update the size of the bitmap to match the
  /// grid size
  void update_bitmap_size();
  /// Initialize the device contexts and bitmap.
  void initialize_context();
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
    IDWriteTextFormat* text_format,
    ID2D1SolidColorBrush* fg_brush,
    ID2D1SolidColorBrush* bg_brush
  );
};

#endif // NVUI_PLATFORM_WINDOWS_DIRECT2DPAINTGRID_HPP
