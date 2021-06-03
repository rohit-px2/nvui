#ifndef NVUI_WINEDITOR_HPP
#define NVUI_WINEDITOR_HPP
#include "editor.hpp"
#include <QBackingStore>
#include <QDebug>
#include <QDesktopWidget>
#include <QPaintEngine>
#include <QPainter>
#include <QImage>
#include <QSize>
#include <limits>
#include <unordered_set>
#include <windows.h>
#include <dwrite.h>
#include <dwrite_1.h>
#include <dwrite_2.h>
#include <dwrite_3.h>
#include <d2d1.h>
#include <d2d1_1.h>
#include <fmt/format.h>
#include <fmt/core.h>
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

using DWriteFactory = IDWriteFactory;

template<class T>
inline void SafeRelease(T** ppT)
{
  if (*ppT)
  {
    (*ppT)->Release();
    *ppT = NULL;
  }
}

class WinEditorArea : public EditorArea
{
public:
  WinEditorArea(
    QWidget* parent = nullptr,
    HLState* state = nullptr,
    Nvim* nv = nullptr
  ): EditorArea(parent, state, nv)
  {
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NativeWindow);
    hwnd = (HWND) winId();
    RECT r;
    GetClientRect(hwnd, &r);
    D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, &d2d_factory);
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(DWriteFactory), reinterpret_cast<IUnknown**>(&factory));
    D2D1_SIZE_U sz = D2D1::SizeU(size().width(), size().height());
    d2d_factory->CreateHwndRenderTarget(
      D2D1::RenderTargetProperties(),
      D2D1::HwndRenderTargetProperties(hwnd, sz),
      &hwnd_target
    );
    const QSize max_size = QDesktopWidget().size();
    D2D1_SIZE_U bitmap_sz = D2D1::SizeU(max_size.width(), max_size.height());
    d2d_factory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(), D2D1::HwndRenderTargetProperties(hwnd, sz), &hwnd_target);
    hwnd_target->QueryInterface(&device_context);
    device_context->CreateBitmap(bitmap_sz, nullptr, 0,
      D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(
          DXGI_FORMAT_B8G8R8A8_UNORM,
          D2D1_ALPHA_MODE_PREMULTIPLIED
        )
      ),
      &dc_bitmap
    );
    //device_context->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
  }

  ~WinEditorArea()
  {
    SafeRelease(&hwnd_target);
    SafeRelease(&d2d_factory);
    SafeRelease(&factory);
    SafeRelease(&text_format);
    SafeRelease(&typography);
    SafeRelease(&device_context);
    SafeRelease(&dc_bitmap);
  }

  QPaintEngine* paintEngine() const override
  {
    return nullptr;
  }
private:
  HWND hwnd = nullptr;
  ID2D1HwndRenderTarget* hwnd_target = nullptr;
  ID2D1Factory* d2d_factory = nullptr;
  DWriteFactory* factory = nullptr;
  IDWriteTextFormat* text_format = nullptr;
  IDWriteTypography* typography = nullptr;
  ID2D1DeviceContext* device_context = nullptr;
  ID2D1Bitmap1* dc_bitmap = nullptr;

  // Override EditorArea draw_grid
  // We can use native Windows stuff here
  template<typename T>
  void draw_grid(const Grid& grid, const QRect& rect, T* target)
  {
    QString buffer;
    buffer.reserve(100);
    const int start_x = rect.x();
    const int start_y = rect.y();
    const int end_x = rect.right();
    const int end_y = rect.bottom();
    const QFontMetrics metrics {font};
    const HLAttr& def_clrs = state->default_colors_get();
    std::uint16_t prev_hl_id = 0;
    const auto get_pos = [&](int x, int y, int num_chars) {
      float left = x * font_width;
      float top = y * font_height;
      float bottom = top + font_height;
      float right = left + (font_width * num_chars);
      return std::make_tuple(D2D1::Point2F(left, top), D2D1::Point2F(right, bottom));
    };
    D2D1_POINT_2F cur_start = {0., 0.};
    const auto clear_buffer = [&](const HLAttr& attr, int x, int y) {
      if (buffer.isEmpty()) return;
      D2D1_POINT_2F cur_pos = D2D1::Point2F(x * font_width, y * font_height);
      draw_text_and_bg(buffer, cur_start, attr, target, cur_pos, def_clrs);
    };
    for(int y = start_y; y <= end_y && y < grid.rows; ++y)
    {
      std::uint16_t prev_hl_id = UINT16_MAX;
      for(int x = start_x; x <= end_x && x < grid.cols; ++x)
      {
        const auto& gc = grid.area[y * grid.cols + x];
        const HLAttr& attr = state->attr_for_id(static_cast<int>(gc.hl_id));
        const auto [top_left, bot_right] = get_pos(grid.x + x, grid.y + y, 1);
        draw_text_and_bg(gc.text, top_left, attr, target, bot_right, def_clrs);
      }
    }
  }
  
  template<typename T>
  void draw_text_and_bg(const QString& text, const D2D1_POINT_2F pt, const HLAttr& attr, T* target, D2D1_POINT_2F cur_pos, const HLAttr& def_clrs)
  {
    IDWriteTextLayout* t_layout = nullptr;
    factory->CreateTextLayout((LPCWSTR)text.utf16(), text.size(), text_format, cur_pos.x - pt.x, cur_pos.y - pt.y, &t_layout);
    DWRITE_TEXT_RANGE text_range {0, (std::uint32_t) text.size()};
    if (attr.font_opts & FontOpts::Italic)
    {
      t_layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, text_range);
    }
    if (attr.font_opts & FontOpts::Bold)
    {
      t_layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, text_range);
    }
    Color fg = attr.has_fg ? attr.foreground : def_clrs.foreground;
    Color bg = attr.has_bg ? attr.background : def_clrs.background;
    if (attr.reverse)
    {
      std::swap(fg, bg);
    }
    // cur_pos's y-value should be the bottom
    D2D1_RECT_F bg_rect = {
      .left = pt.x,
      .top = pt.y,
      .right = cur_pos.x,
      .bottom = cur_pos.y
    };
    //std::cout << "Bg rect size: (" << bg_rect.right - bg_rect.left << ", " << bg_rect.bottom - bg_rect.top << ")\n";
    ID2D1SolidColorBrush* fg_brush = nullptr;
    ID2D1SolidColorBrush* bg_brush = nullptr;
    target->CreateSolidColorBrush(D2D1::ColorF(fg.to_uint32()), &fg_brush);
    target->CreateSolidColorBrush(D2D1::ColorF(bg.to_uint32()), &bg_brush);
    target->FillRectangle(bg_rect, bg_brush);
    target->DrawTextLayout(pt, t_layout, fg_brush, D2D1_DRAW_TEXT_OPTIONS_NONE);
    fg_brush->Release();
    bg_brush->Release();
    t_layout->Release();
  }

  void clear_grid(const Grid& grid, const QRect& rect, ID2D1RenderTarget* target)
  {
    const HLAttr& def_clrs = state->default_colors_get();
    ID2D1SolidColorBrush* bg_brush;
    Color bg = def_clrs.background;
    target->CreateSolidColorBrush(D2D1::ColorF(bg.to_uint32()), &bg_brush);
    D2D1_RECT_F bg_rect {
      .left = (float) (grid.x + rect.left()) * font_width,
      .top = (float) (grid.y + rect.top()) * font_height,
      .right = (float) (grid.x + rect.right()) * font_width,
      .bottom = (float) (grid.y + rect.bottom()) * font_height
    };
    target->FillRectangle(bg_rect, bg_brush);
  }

protected:
  void paintEvent(QPaintEvent* event) override
  {
    SafeRelease(&text_format);
    factory->CreateTextFormat(
      (LPCWSTR) font.family().utf16(),
      NULL,
      DWRITE_FONT_WEIGHT_NORMAL,
      DWRITE_FONT_STYLE_NORMAL,
      DWRITE_FONT_STRETCH_NORMAL,
      font.pointSizeF() * (96.0f / 72.0f),
      L"en-us",
      &text_format
    );
    Q_UNUSED(event);
    ID2D1Image* old_target;
    device_context->GetTarget(&old_target);
    device_context->SetTarget(dc_bitmap);
    device_context->BeginDraw();
    while(!events.empty())
    {
      const PaintEventItem& event = events.front();
      const Grid* grid = find_grid(event.grid_num);
      assert(grid);
      if (event.type == PaintKind::Clear)
      {
        clear_grid(*grid, event.rect, device_context);
      }
      else if (event.type == PaintKind::Redraw)
      {
        for(const auto& grid : grids)
        {
          draw_grid(grid, QRect(0, 0, grid.cols, grid.rows), device_context);
        }
      }
      else
      {
        draw_grid(*grid, event.rect, device_context);
      }
      events.pop();
    }
    device_context->SetTarget(old_target);
    device_context->DrawBitmap(dc_bitmap);
    device_context->EndDraw();
    old_target->Release();
  }

  void resizeEvent(QResizeEvent* event) override
  {
    auto sz = D2D1::SizeU(event->size().width(), event->size().height());
    hwnd_target->Resize(sz);
    //update();
  }
};

#endif // NVUI_WINEDITOR_HPP
