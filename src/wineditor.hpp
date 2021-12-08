#ifndef NVUI_WINEDITOR_HPP
#define NVUI_WINEDITOR_HPP
#include "editor.hpp"
#include "utils.hpp"
#include <DWrite.h>
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
#include <winuser.h>
#include "platform/windows/direct2dpaintgrid.hpp"

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

static void
create_default_bitmap(ID2D1DeviceContext* t, ID2D1Bitmap1** ppb, D2D1_SIZE_U size)
{
  t->CreateBitmap(size, nullptr, 0,
    D2D1::BitmapProperties1(
      D2D1_BITMAP_OPTIONS_TARGET,
      D2D1::PixelFormat(
        DXGI_FORMAT_B8G8R8A8_UNORM,
        D2D1_ALPHA_MODE_PREMULTIPLIED
      )
    ),
    ppb
  );
}

/// The WinEditorArea is a version of the EditorArea that only works on
/// Windows, since it uses Direct2D and DirectWrite for rendering instead
/// of Qt's cross-platform solution.
class WinEditorArea : public EditorArea
{
  using u32 = std::uint32_t;
  using d2pt = D2D1_POINT_2F;
  using d2clr = D2D1::ColorF;
  static constexpr float default_dpi = 96.0f;
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
    d2d_factory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(), D2D1::HwndRenderTargetProperties(hwnd, sz), &hwnd_target);
    hwnd_target->QueryInterface(&device_context);
    device_context->GetDevice(&device);
    device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_ENABLE_MULTITHREADED_OPTIMIZATIONS, &mtd_context);
    device_context->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    device_context->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    // Setting a higher dpi causes it to stretch the text, the workaround is just to ignore the
    // system dpi. However the font size changes based on the dpi.
    device_context->SetDpi(default_dpi, default_dpi);
    create_default_bitmap(mtd_context, &dc_bitmap, sz);
    mtd_context->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    mtd_context->SetTarget(dc_bitmap);
  }

  ~WinEditorArea()
  {
    for(auto& tf : text_formats) SafeRelease(&tf);
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
  
  FontDimensions font_dimensions() const override
  {
    if (font_width_f <= 0.f)
    {
      return EditorArea::font_dimensions();
    }
    else
    {
      return {font_width_f, font_height_f};
    }
  }
  
  /// Return the list of text formats for font fallback.
  const auto& fallback_list() const { return text_formats; }
  /// Create a new device context and bitmap, associate the two
  /// (device context paints to the bitmap), and initializes the bitmap
  /// with the given size.
  void create_context(
    ID2D1DeviceContext** context,
    ID2D1Bitmap1** bitmap,
    std::uint32_t width,
    std::uint32_t height
  )
  {
    device->CreateDeviceContext(
      D2D1_DEVICE_CONTEXT_OPTIONS_FORCE_DWORD,
      context
    );
    SafeRelease(bitmap);
    create_default_bitmap(*context, bitmap, {width, height});
    (*context)->SetTarget(*bitmap);
    (*context)->SetDpi(default_dpi, default_dpi);
  }
  /// Resizes bitmap to the new width and height.
  /// *bitmap is modified to point to the newly-created bitmap.
  void resize_bitmap(
      ID2D1DeviceContext* context,
      ID2D1Bitmap1** bitmap,
      std::uint32_t width,
      std::uint32_t height
  )
  {
    SafeRelease(bitmap);
    create_default_bitmap(context, bitmap, {width, height});
  }
  /// Returns the internal DWriteFactory object.
  DWriteFactory* dwrite_factory() { return factory; }
  auto linespacing() const { return linespace; }
  auto charspacing() const { return charspace; }
private:
  HWND hwnd = nullptr;
  ID2D1HwndRenderTarget* hwnd_target = nullptr;
  ID2D1Factory* d2d_factory = nullptr;
  DWriteFactory* factory = nullptr;
  IDWriteTextFormat* text_format = nullptr;
  std::vector<IDWriteTextFormat*> text_formats;
  IDWriteTypography* typography = nullptr;
  ID2D1DeviceContext* device_context = nullptr;
  ID2D1Bitmap1* dc_bitmap = nullptr;
  ID2D1Device* device = nullptr;
  ID2D1DeviceContext* mtd_context = nullptr;
  QString font_name = "";
  float font_width_f = -1.f;
  float font_height_f = -1.f;
  float x_dpi = default_dpi;

  void draw_cursor(ID2D1RenderTarget* target)
  {
    auto grid_num = neovim_cursor.grid_num();
    if (grid_num < 0) return;
    if (auto* grid = static_cast<D2DPaintGrid*>(find_grid(grid_num)))
    {
      grid->draw_cursor(target, neovim_cursor);
    }
  }

  /// Overrides the default (QPaintGrid) creation to make a D2DPaintGrid.
  /// In the paintEvent we cast the GridBase ptr to a D2DPaintGrid ptr and
  /// draw its contents to the screen (if it's not hidden).
  void create_grid(u16 x, u16 y, u16 w, u16 h, u16 id) override
  {
    grids.push_back(std::make_unique<D2DPaintGrid>(this, x, y, w, h, id));
  }

protected:
  void update_font_metrics(bool update_fonts) override
  {
    x_dpi = static_cast<float>(logicalDpiX());
    // Create a text format from a QFont, modifies *tf to hold the new text format
    // If *tf contained a text format before it should be released before calling this
    constexpr auto create_format = 
      [](DWriteFactory* factory, const QFont& f, IDWriteTextFormat** tf, float dpi = default_dpi) {
        HRESULT hr = factory->CreateTextFormat(
          (LPCWSTR) f.family().utf16(),
          NULL,
          DWRITE_FONT_WEIGHT_NORMAL,
          DWRITE_FONT_STYLE_NORMAL,
          DWRITE_FONT_STRETCH_NORMAL,
          f.pointSizeF() * (dpi / 72.0f),
          L"en-us",
          tf
        );
        return hr;
    };
    HRESULT hr;
    EditorArea::update_font_metrics();
    if (update_fonts)
    {
      for(auto& tf : text_formats) SafeRelease(&tf);
      text_formats.clear();
      text_formats.resize(fonts.size());
      for(std::size_t i = 0; i < fonts.size(); ++i)
      {
        const QFont& idx_font = fonts[i].font();
        hr = create_format(factory, idx_font, &text_formats[i], x_dpi);
        if (FAILED(hr))
        {
          fmt::print("Create format failed for family {}\n", idx_font.family().toStdString());
          text_formats[i] = nullptr;
        }
      }
    }
    SafeRelease(&text_format);
    create_format(factory, font, &text_format, x_dpi);
    constexpr const wchar_t* text = L"W";
    constexpr std::uint32_t len = 1;
    IDWriteTextLayout* text_layout = nullptr;
    hr = factory->CreateTextLayout(
      text, len, text_format, font_width * 2, font_height * 2, &text_layout
    );
    if (SUCCEEDED(hr) && text_layout != nullptr)
    {
      DWRITE_HIT_TEST_METRICS ht_metrics;
      float ignore;
      text_layout->HitTestTextPosition(0, 0, &ignore, &ignore, &ht_metrics);
      font_width_f = (ht_metrics.width + charspace);
      font_height_f = std::ceil(ht_metrics.height + float(linespace));
    }
    SafeRelease(&text_layout);
  }

  void default_colors_changed(QColor fg, QColor bg) override
  {
    (void)fg; (void)bg;
    send_redraw();
  }
  
  QSize to_rc(const QSize& pixel_size) override
  {
    int new_width = float(pixel_size.width()) / font_width_f;
    int new_height = float(pixel_size.height()) / font_height_f;
    return {new_width, new_height};
  }

  void paintEvent(QPaintEvent* event) override
  {
    event->accept();
    device_context->BeginDraw();
    auto bg = default_bg().rgb();
    ID2D1SolidColorBrush* bg_brush = nullptr;
    device_context->CreateSolidColorBrush(D2D1::ColorF(bg), &bg_brush);
    auto r = D2D1::RectF(0, 0, width(), height());
    auto grid_clip_rect = D2D1::RectF(0, 0, cols * font_width_f, rows * font_height_f);
    device_context->FillRectangle(r, bg_brush);
    device_context->PushAxisAlignedClip(grid_clip_rect, D2D1_ANTIALIAS_MODE_ALIASED);
    SafeRelease(&bg_brush);
    for(auto& grid_base : grids)
    {
      auto* grid = static_cast<D2DPaintGrid*>(grid_base.get());
      if (!grid->hidden)
      {
        grid->process_events();
        grid->render(device_context);
      }
    }
    device_context->PopAxisAlignedClip();
    if (!neovim_cursor.hidden() && cmdline.isHidden())
    {
      draw_cursor(device_context);
    }
    device_context->EndDraw();
    if (!popup_menu.hidden()) draw_popup_menu();
    else popup_menu.hide();
  }

  void resizeEvent(QResizeEvent* event) override
  {
    auto sz = D2D1::SizeU(event->size().width(), event->size().height());
    hwnd_target->Resize(sz);
    update();
  }
};

#endif // NVUI_WINEDITOR_HPP
