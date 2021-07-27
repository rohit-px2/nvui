#ifndef NVUI_WINEDITOR_HPP
#define NVUI_WINEDITOR_HPP
#include "editor.hpp"
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
  
  std::tuple<float, float> font_dimensions() const override
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

  // Override EditorArea draw_grid
  // We can use native Windows stuff here
  inline void draw_grid(const Grid& grid, const QRect& rect, ID2D1RenderTarget* target, std::unordered_set<int>& drawn)
  {
    using d2pt = D2D1_POINT_2F;
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
    const auto draw_buf = [&](const d2pt& start, const d2pt& end, const HLAttr& main, const HLAttr& fallback, std::uint32_t font_idx) {
      if (!buffer.isEmpty())
      {
        draw_text_and_bg(buffer, text_formats[font_idx], main, fallback, start, end, target);
        buffer.clear();
      }
    };
    for(int y = start_y; y <= end_y && y < grid.rows; ++y)
    {
      // Check if we already drew the line
      d2pt cur_start = {grid.x * font_width_f, (grid.y + y) * font_height_f};
      if (drawn.contains(grid.y + y)) continue;
      else drawn.insert(grid.y + y);
      std::uint16_t prev_hl_id = UINT16_MAX;
      std::uint32_t cur_font_idx = 0;
      for(int x = 0; x < grid.cols; ++x)
      {
        const auto& gc = grid.area[y * grid.cols + x];
        // The second condition is for supporting Nerd fonts
        auto font_idx = font_for_ucs(gc.ucs);
        if (font_idx != cur_font_idx && !(gc.text.isEmpty() || gc.text.at(0).isSpace()))
        {
          auto&& [top_left, bot_right] = get_pos(grid.x + x, grid.y + y, 1);
          d2pt buf_end = {top_left.x + font_width_f, top_left.y + font_height_f};
          draw_buf(cur_start, buf_end, state->attr_for_id(prev_hl_id), def_clrs, cur_font_idx);
          cur_start = top_left;
          cur_font_idx = font_idx;
        }
        if (gc.double_width)
        {
          auto [top_left, bot_right] = get_pos(grid.x + x, grid.y + y, 2);
          // Directwrite sometimes makes the content overflow (draws the last char
          // of the text on the next line). Since we do clipping this shows up as
          // the last character before a double-width character disappearing. This
          // doesn't happen all the time, but I've seen it happen.
          // To solve this we make the ending point a little further to the right than
          // it should be.
          d2pt buf_end = {top_left.x + font_width_f, top_left.y + font_height_f};
          draw_buf(cur_start, buf_end, state->attr_for_id(prev_hl_id), def_clrs, cur_font_idx);
          buffer.append(gc.text);
          draw_buf(top_left, bot_right, state->attr_for_id(gc.hl_id), def_clrs, cur_font_idx);
          prev_hl_id = gc.hl_id;
          ++x;
          cur_start = {bot_right.x, bot_right.y - font_height_f};
        }
        else if (prev_hl_id == gc.hl_id)
        {
          buffer.append(gc.text);
          continue;
        }
        else
        {
          auto [top_left, bot_right] = get_pos(grid.x + x, grid.y + y, 1);
          draw_buf(cur_start, bot_right, state->attr_for_id(prev_hl_id), def_clrs, cur_font_idx);
          cur_start = top_left;
          buffer.append(gc.text);
        }
        prev_hl_id = gc.hl_id;
      }
      auto [top_left, bot_right] = get_pos(grid.x + (grid.cols-1), grid.y + y, 1);
      draw_buf(cur_start, bot_right, state->attr_for_id(prev_hl_id), def_clrs, cur_font_idx);
    }
  }
  
  template<typename T>
  inline int draw_text_and_bg(
    const QString& text,
    IDWriteTextFormat* format,
    const HLAttr& attr,
    const HLAttr& def_clrs,
    const D2D1_POINT_2F start,
    const D2D1_POINT_2F end,
    T* target
  )
  {
    IDWriteTextLayout1* t_layout = nullptr;
    factory->CreateTextLayout((LPCWSTR)text.utf16(), text.size(), format, end.x - start.x, end.y - start.y, reinterpret_cast<IDWriteTextLayout**>(&t_layout));
    DWRITE_TEXT_RANGE text_range {0, (std::uint32_t) text.size()};
    if (charspace)
    {
      t_layout->SetCharacterSpacing(0, float(charspace), 0, text_range);
    }
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
    Color fg = attr.fg().value_or(*def_clrs.fg());
    Color bg = attr.bg().value_or(*def_clrs.bg());
    if (attr.reverse)
    {
      std::swap(fg, bg);
    }
    // cur_pos's y-value should be the bottom
    D2D1_RECT_F bg_rect = D2D1::RectF(start.x, start.y, end.x, end.y);
    ID2D1SolidColorBrush* fg_brush = nullptr;
    ID2D1SolidColorBrush* bg_brush = nullptr;
    target->CreateSolidColorBrush(D2D1::ColorF(fg.to_uint32()), &fg_brush);
    target->CreateSolidColorBrush(D2D1::ColorF(bg.to_uint32()), &bg_brush);
    //target->PushAxisAlignedClip(bg_rect, D2D1_ANTIALIAS_MODE_ALIASED);
    target->FillRectangle(bg_rect, bg_brush);
    const float offset = float(linespace) / 2.f;
    D2D1_POINT_2F text_pt = {start.x, start.y + offset};
    target->DrawTextLayout(text_pt, t_layout, fg_brush, D2D1_DRAW_TEXT_OPTIONS_NONE);
    //target->PopAxisAlignedClip();
    SafeRelease(&fg_brush);
    SafeRelease(&bg_brush);
    SafeRelease(&t_layout);
    return end.x - start.x;
  }

  inline void clear_grid(const Grid& grid, const QRect& rect, ID2D1RenderTarget* target)
  {
    const HLAttr& def_clrs = state->default_colors_get();
    ID2D1SolidColorBrush* bg_brush;
    Color bg = *def_clrs.background;
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

  void clear_old_cursor(ID2D1RenderTarget* target)
  {
    const auto old_rect_opt = neovim_cursor.old_rect(font_width_f, font_height_f);
    if (!old_rect_opt.has_value()) return;
    const CursorRect old_rect = old_rect_opt.value();
    ID2D1SolidColorBrush* bg_brush = nullptr;
    const Color& bg = *state->default_colors_get().background;
    target->CreateSolidColorBrush(D2D1::ColorF(bg.to_uint32()), &bg_brush);
    const QRectF& r = old_rect.rect;
    auto fill_rect = D2D1::RectF(r.left(), r.top(), r.right(), r.bottom());
    target->FillRectangle(fill_rect, bg_brush);
    SafeRelease(&bg_brush);
  }

  void draw_cursor(ID2D1RenderTarget* target)
  {
    const auto pos_opt = neovim_cursor.pos();
    if (!pos_opt) return;
    const auto pos = pos_opt.value();
    Grid* grid = find_grid(pos.grid_num);
    if (!grid) return;
    const auto& gc = grid->area[pos.row * grid->cols + pos.col];
    float scale_factor = 1.0f;
    if (gc.double_width) scale_factor = 2.0f;
    const CursorRect rect = neovim_cursor.rect(font_width_f, font_height_f, scale_factor).value();
    ID2D1SolidColorBrush* bg_brush = nullptr;
    const HLAttr& def_clrs = state->default_colors_get();
    HLAttr attr = state->attr_for_id(rect.hl_id);
    Color bg;
    if (rect.hl_id == 0) bg = *def_clrs.foreground;
    else
    {
      if (attr.reverse)
      {
        bg = attr.fg().value_or(*def_clrs.fg());
      }
      else
      {
        bg = attr.bg().value_or(*def_clrs.bg());
      }
    }
    target->CreateSolidColorBrush(D2D1::ColorF(bg.to_uint32()), &bg_brush);
    const QRectF& r = rect.rect;
    auto fill_rect = D2D1::RectF(r.left(), r.top(), r.right(), r.bottom());
    target->PushAxisAlignedClip(fill_rect, D2D1_ANTIALIAS_MODE_ALIASED);
    target->FillRectangle(fill_rect, bg_brush);
    target->PopAxisAlignedClip();
    if (rect.should_draw_text)
    {
      // If the rect exists, the pos must exist as well.
       std::uint32_t font_idx = font_for_ucs(gc.ucs);
      assert(font_idx < text_formats.size());
      if (rect.hl_id == 0) attr.reverse = true;
      const auto start = D2D1::Point2F(fill_rect.left, fill_rect.top);
      const auto end = D2D1::Point2F(fill_rect.right, fill_rect.bottom);
      draw_text_and_bg(gc.text, text_formats[font_idx], attr, def_clrs, start, end, target);
    }
    SafeRelease(&bg_brush);
  }

protected:
  void update_font_metrics(bool update_fonts) override
  {
    // Create a text format from a QFont, modifies *tf to hold the new text format
    // If *tf contained a text format before it should be released before calling this
    constexpr auto create_format = 
      [](DWriteFactory* factory, const QFont& f, IDWriteTextFormat** tf) {
        HRESULT hr = factory->CreateTextFormat(
          (LPCWSTR) f.family().utf16(),
          NULL,
          DWRITE_FONT_WEIGHT_NORMAL,
          DWRITE_FONT_STYLE_NORMAL,
          DWRITE_FONT_STRETCH_NORMAL,
          f.pointSizeF() * (96.0f / 72.0f),
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
        hr = create_format(factory, idx_font, &text_formats[i]);
        if (FAILED(hr))
        {
          fmt::print("Create format failed for family {}\n", idx_font.family().toStdString());
          text_formats[i] = nullptr;
        }
      }
    }
    SafeRelease(&text_format);
    create_format(factory, font, &text_format);
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
      font_width_f = ht_metrics.width + charspace;
      font_height_f = std::ceilf(ht_metrics.height + float(linespace));
    }
    SafeRelease(&text_layout);
  }

  void default_colors_changed(QColor fg, QColor bg) override
  {
    Q_UNUSED(fg);
    Q_UNUSED(bg);
    mtd_context->BeginDraw();
    auto size = mtd_context->GetSize();
    D2D1_RECT_F rect {
      .left = 0.f, .top = 0.f, .right = size.width, .bottom = size.height
    };
    ID2D1SolidColorBrush* bg_brush = nullptr;
    const HLAttr& attr = state->default_colors_get();
    mtd_context->CreateSolidColorBrush(D2D1::ColorF(attr.background->to_uint32()), &bg_brush);
    mtd_context->FillRectangle(rect, bg_brush);
    mtd_context->EndDraw();
    events.push({PaintKind::Redraw, 0, QRect()});
    SafeRelease(&bg_brush);
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
        drawn_rows.clear();
        for(const auto& grid : grids)
        {
          draw_grid(grid, QRect(0, 0, grid.cols, grid.rows), mtd_context, drawn_rows);
        }
        drawn_rows.clear();
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
    // Bitmaps have to be re-created
    SafeRelease(&dc_bitmap);
    create_default_bitmap(mtd_context, &dc_bitmap, sz);
    mtd_context->SetTarget(dc_bitmap);
    // Fill with the background color so it doesn't look ugly
    auto r = D2D1::RectF(0, 0, sz.width, sz.height);
    const auto clr = D2D1::ColorF(state->default_colors_get().background->to_uint32());
    ID2D1SolidColorBrush* bg_brush = nullptr;
    mtd_context->BeginDraw();
    mtd_context->CreateSolidColorBrush(clr, &bg_brush);
    mtd_context->FillRectangle(r, bg_brush);
    mtd_context->EndDraw();
    SafeRelease(&bg_brush);
    events.push({PaintKind::Redraw, 0, QRect()});
  }
};

#endif // NVUI_WINEDITOR_HPP
