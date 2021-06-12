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
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d_factory);
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
    device_context->GetDevice(&device);
    device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_ENABLE_MULTITHREADED_OPTIMIZATIONS, &mtd_context);
    mtd_context->CreateBitmap(bitmap_sz, nullptr, 0,
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
    mtd_context->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    mtd_context->SetTarget(dc_bitmap);
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
    SafeRelease(&device);
    SafeRelease(&mtd_context);
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
  ID2D1Device* device = nullptr;
  ID2D1DeviceContext* mtd_context = nullptr;
  QString font_name = "";
  float font_width_f;
  float font_height_f;

  // Override EditorArea draw_grid
  // We can use native Windows stuff here
  template<typename T>
  inline void draw_grid(const Grid& grid, const QRect& rect, T* target, std::unordered_set<int>& drawn)
  {
    QString buffer;
    buffer.reserve(100);
    const int start_x = rect.x();
    const int start_y = rect.y();
    const int end_x = rect.right();
    const int end_y = rect.bottom();
    const QFontMetrics metrics {font};
    const HLAttr& def_clrs = state->default_colors_get();
    const auto get_pos = [&](int x, int y, int num_chars) {
      float left = x * font_width_f;
      float top = y * font_height_f;
      float bottom = top + font_height_f;
      float right = left + (font_width_f * num_chars);
      return std::make_tuple(D2D1::Point2F(left, top), D2D1::Point2F(right, bottom));
    };
    D2D1_POINT_2F cur_start = {0., 0.};
    for(int y = start_y; y <= end_y && y < grid.rows; ++y)
    {
      // Check if we already drew the line
      if (drawn.contains(grid.y + y)) continue;
      else drawn.insert(grid.y + y);
      std::uint16_t prev_hl_id = UINT16_MAX;
      for(int x = 0; x < grid.cols; ++x)
      {
        const auto& gc = grid.area[y * grid.cols + x];
        if (prev_hl_id == gc.hl_id)
        {
          buffer.append(gc.text);
          continue;
        }
        else
        {
          auto [top_left, bot_right] = get_pos(grid.x + x, grid.y + y, 1);
          if (!buffer.isEmpty())
          {
            const HLAttr& attr = state->attr_for_id(prev_hl_id);
            draw_text_and_bg(buffer, cur_start, attr, target, bot_right, def_clrs);
            buffer.clear();
          }
          cur_start = top_left;
          buffer.append(gc.text);
        }
        prev_hl_id = gc.hl_id;
      }
      if (!buffer.isEmpty())
      {
        const HLAttr& attr = state->attr_for_id(prev_hl_id);
        auto [top_left, bot_right] = get_pos(grid.x + (grid.cols-1), grid.y + y, 1);
        draw_text_and_bg(buffer, cur_start, attr, target, bot_right, def_clrs);
        buffer.clear();
      }
    }
  }
  
  template<typename T>
  inline int draw_text_and_bg(const QString& text, const D2D1_POINT_2F pt, const HLAttr& attr, T* target, D2D1_POINT_2F cur_pos, const HLAttr& def_clrs)
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
    if (attr.font_opts & FontOpts::Underline)
    {
      t_layout->SetUnderline(true, text_range);
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
    D2D1_RECT_F clip_rect = D2D1::RectF(pt.x, pt.y, cur_pos.x, cur_pos.y);
    target->PushAxisAlignedClip(clip_rect, D2D1_ANTIALIAS_MODE_ALIASED);
    target->FillRectangle(bg_rect, bg_brush);
    target->DrawTextLayout(pt, t_layout, fg_brush, D2D1_DRAW_TEXT_OPTIONS_NONE);
    target->PopAxisAlignedClip();
    fg_brush->Release();
    bg_brush->Release();
    t_layout->Release();
    return cur_pos.x - pt.x;
  }

  inline void clear_grid(const Grid& grid, const QRect& rect, ID2D1RenderTarget* target)
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
    SafeRelease(&bg_brush);
  }

protected:
  void update_font_metrics() override
  {
    EditorArea::update_font_metrics();
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
    constexpr const wchar_t* text = L"W";
    constexpr std::uint32_t len = 1;
    IDWriteTextLayout* text_layout = nullptr;
    HRESULT hr = factory->CreateTextLayout(text, len, text_format, font_width * 2, font_height * 2, &text_layout);
    if (SUCCEEDED(hr) && text_layout != nullptr)
    {
      DWRITE_HIT_TEST_METRICS ht_metrics;
      float ignore;
      text_layout->HitTestTextPosition(0, 0, &ignore, &ignore, &ht_metrics);
      font_width_f = ht_metrics.width;
      font_height_f = ht_metrics.height + float(linespace);
    }
    SafeRelease(&text_layout);
  }
  
  QSize to_rc(const QSize& pixel_size) override
  {
    int new_width = float(pixel_size.width()) / font_width_f;
    int new_height = float(pixel_size.height()) / font_height_f;
    return {new_width, new_height};
  }

  void paintEvent(QPaintEvent* event) override
  {
    std::unordered_set<int> drawn_rows;
    Q_UNUSED(event);
    mtd_context->BeginDraw();
    while(!events.empty())
    {
      const PaintEventItem& event = events.front();
      const Grid* grid = find_grid(event.grid_num);
      assert(grid);
      if (event.type == PaintKind::Clear)
      {
        clear_grid(*grid, event.rect, mtd_context);
      }
      else if (event.type == PaintKind::Redraw)
      {
        for(const auto& grid : grids)
        {
          draw_grid(grid, QRect(0, 0, grid.cols, grid.rows), mtd_context, drawn_rows);
        }
      }
      else
      {
        draw_grid(*grid, event.rect, mtd_context, drawn_rows);
      }
      events.pop();
    }
    mtd_context->EndDraw();
    device_context->BeginDraw();
    device_context->DrawBitmap(dc_bitmap);
    device_context->EndDraw();
  }

  void resizeEvent(QResizeEvent* event) override
  {
    auto sz = D2D1::SizeU(event->size().width(), event->size().height());
    hwnd_target->Resize(sz);
    //update();
  }
};

#endif // NVUI_WINEDITOR_HPP
