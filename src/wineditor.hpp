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
#include <unordered_set>
#include <windows.h>
#include <dwrite.h>
#include <dwrite_1.h>
#include <dwrite_2.h>
#include <dwrite_3.h>
#include <d2d1.h>
#include <d2d1_1.h>
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
    qDebug() << "Qt Size: " << size() << "\n";
    setAttribute(Qt::WA_PaintOnScreen);
    //setAttribute(Qt::WA_NativeWindow);
    hwnd = (HWND) winId();
    RECT r;
    GetClientRect(hwnd, &r);
    std::cout << "Windows size: (" << r.right - r.left << ", " << r.bottom - r.top << ")\n";
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
    D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS options {};
    hwnd_target->CreateCompatibleRenderTarget(NULL, &bitmap_sz, NULL, options, &bitmap_target);
    //bitmap_target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    bitmap_target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    hwnd_target->QueryInterface(&device_context);
  }

  ~WinEditorArea()
  {
    SafeRelease(&bitmap_target);
    SafeRelease(&hwnd_target);
    SafeRelease(&d2d_factory);
    SafeRelease(&factory);
    SafeRelease(&text_format);
    SafeRelease(&typography);
    SafeRelease(&bitmap);
    SafeRelease(&device_context);
  }

  QPaintEngine* paintEngine() const override
  {
    return nullptr;
  }
private:
  HWND hwnd = nullptr;
  ID2D1BitmapRenderTarget* bitmap_target = nullptr;
  ID2D1HwndRenderTarget* hwnd_target = nullptr;
  ID2D1Factory* d2d_factory = nullptr;
  DWriteFactory* factory = nullptr;
  IDWriteTextFormat* text_format = nullptr;
  IDWriteTypography* typography = nullptr;
  ID2D1DeviceContext* device_context = nullptr;
  ID2D1Bitmap* bitmap = nullptr;
  int rows = -1;
  int cols = -1;

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
    std::uint16_t prev_hl_id = 0;
    for(int y = start_y; y <= end_y && y < grid.rows; ++y)
    {
      for(int x = start_x; x <= end_x && x < grid.cols; ++x)
      {
        const auto& gc = grid.area[y * grid.cols + x];
        const HLAttr& attr = state->attr_for_id(static_cast<int>(gc.hl_id));
        float left = ((float)(grid.x + x) * (float)font_width);
        float top = ((float)(grid.y + y) * (float)font_height);
        float bottom = top + font_height;
        float right = left + font_width;
        const auto top_left = D2D1::Point2F(left, top);
        const auto bot_right = D2D1::Point2F(right, bottom);
        draw_text_and_bg(gc.text, top_left, attr, target, bot_right);
      }
    }
  }
  
  template<typename T>
  void draw_text_and_bg(const QString& text, const D2D1_POINT_2F pt, const HLAttr& attr, T* target, D2D1_POINT_2F cur_pos)
  {
    const HLAttr& def_clrs = state->default_colors_get();
    IDWriteTextLayout* t_layout = nullptr;
    factory->CreateTextLayout((LPCWSTR)text.utf16(), text.size(), text_format, cur_pos.x - pt.x, cur_pos.y - pt.y, &t_layout);
    DWRITE_TEXT_RANGE text_range {0, (std::uint32_t) text.size()};
    t_layout->SetFontSize(font.pointSizeF() * (96.0f / 72.0f), text_range);
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
    auto r = D2D1::Point2F(pt.x, pt.y);
    target->DrawTextLayout(r, t_layout, fg_brush, D2D1_DRAW_TEXT_OPTIONS_NONE);
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

public slots:
  void resized(QSize size)
  {
    assert(nvim);
    const QSize new_rc = to_rc(size);
    if (new_rc.width() == cols && new_rc.height() == rows) return;
    nvim->resize(new_rc.width(), new_rc.height());
    //events.push(PaintEventItem {PaintKind::Redraw, std::numeric_limits<std::uint16_t>::max(), QRect()});
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
    bitmap_target->SetTransform(D2D1::IdentityMatrix());
    Q_UNUSED(event);
    bitmap_target->BeginDraw();
    while(!events.empty())
    {
      const PaintEventItem& event = events.front();
      const Grid* grid = find_grid(event.grid_num);
      assert(grid);
      if (event.type == PaintKind::Clear)
      {
        //clear_grid(*grid, event.rect, bitmap_target);
      }
      else if (event.type == PaintKind::Redraw)
      {
        //for(const auto& grid : grids)
        //{
          //draw_grid(grid, QRect(0, 0, grid.cols, grid.rows), bitmap_target);
        //}
      }
      else
      {
        draw_grid(*grid, event.rect, bitmap_target);
      }
      events.pop();
    }
    bitmap_target->EndDraw();
    bitmap_target->GetBitmap(&bitmap);
    device_context->BeginDraw();
    device_context->Clear();
    device_context->SetTransform(D2D1::IdentityMatrix());
    device_context->DrawBitmap(bitmap);
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
