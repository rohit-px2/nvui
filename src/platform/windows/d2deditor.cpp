#include "d2deditor.hpp"
#include <windows.h>
#include <d2d1.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <dwrite_1.h>
#include <wrl/client.h>
#include "nvim_utils.hpp"

using DWriteFactory = IDWriteFactory;

static constexpr float default_dpi = 96.0f;

using Microsoft::WRL::ComPtr;

static ComPtr<IDWriteFont>
font_from_name(const std::wstring& name, IDWriteFontCollection* collection)
{
  u32 index = 0;
  BOOL exists = false;
  collection->FindFamilyName(name.c_str(), &index, &exists);
  if (!exists) return nullptr;
  ComPtr<IDWriteFontFamily> ffamily = nullptr;
  collection->GetFontFamily(index, &ffamily);
  if (!ffamily) return nullptr;
  ComPtr<IDWriteFont> font = nullptr;
  ffamily->GetFirstMatchingFont(
    DWRITE_FONT_WEIGHT_NORMAL,
    DWRITE_FONT_STRETCH_NORMAL,
    DWRITE_FONT_STYLE_NORMAL,
    &font
  );
  return font;
}

static ComPtr<IDWriteTextFormat>
format_from_name(
  const std::wstring& name,
  float pointsize,
  float dpi,
  IDWriteFactory* factory
)
{
  ComPtr<IDWriteTextFormat> format = nullptr;
  factory->CreateTextFormat(
    name.c_str(),
    nullptr,
    DWRITE_FONT_WEIGHT_NORMAL,
    DWRITE_FONT_STYLE_NORMAL,
    DWRITE_FONT_STRETCH_NORMAL,
    pointsize * (dpi / 72.0f),
    L"en-us",
    &format
  );
  return format;
}

D2DEditor::D2DEditor(
  int cols,
  int rows,
  std::unordered_map<std::string, bool> capabilities,
  std::string nvim_path,
  std::vector<std::string> nvim_args,
  QWidget* parent,
  bool vsync
)
: QWidget(parent),
  QtEditorUIBase(*this, cols, rows, std::move(capabilities),
    std::move(nvim_path), std::move(nvim_args))
{
  setAttribute(Qt::WA_PaintOnScreen);
  setAttribute(Qt::WA_InputMethodEnabled);
  setAttribute(Qt::WA_OpaquePaintEvent);
  setAutoFillBackground(false);
  setAcceptDrops(true);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setFocusPolicy(Qt::StrongFocus);
  setFocus();
  setMouseTracking(true);
  HWND hwnd = (HWND) winId();
  D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_factory.GetAddressOf());
  DWriteCreateFactory(
    DWRITE_FACTORY_TYPE_SHARED,
    __uuidof(DWriteFactory),
    reinterpret_cast<IUnknown**>(dw_factory.GetAddressOf())
  );
  D2D1_SIZE_U sz = D2D1::SizeU(width(), height());
  auto hwnd_properties = D2D1::HwndRenderTargetProperties(hwnd, sz);
  if (!vsync)
  {
    hwnd_properties.presentOptions = D2D1_PRESENT_OPTIONS_IMMEDIATELY;
  }
  d2d_factory->CreateHwndRenderTarget(
    D2D1::RenderTargetProperties(),
    hwnd_properties,
    &hwnd_target
  );
  hwnd_target->QueryInterface(device_context.GetAddressOf());
  device_context->GetDevice(device.GetAddressOf());
  device_context->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
  device_context->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
  device_context->SetDpi(default_dpi, default_dpi);
}

D2DEditor::~D2DEditor() = default;

void D2DEditor::resizeEvent(QResizeEvent* event)
{
  Base::handle_nvim_resize(event);
  hwnd_target->Resize(D2D1::SizeU(width(), height()));
  QWidget::resizeEvent(event);
}

void D2DEditor::mousePressEvent(QMouseEvent* event)
{
  Base::handle_mouse_press(event);
  if (cursor() != Qt::ArrowCursor)
  {
    QWidget::mousePressEvent(event);
  }
}

void D2DEditor::mouseReleaseEvent(QMouseEvent* event)
{
  Base::handle_mouse_release(event);
  QWidget::mouseReleaseEvent(event);
}

void D2DEditor::wheelEvent(QWheelEvent* event)
{
  Base::handle_wheel(event);
  QWidget::wheelEvent(event);
}

void D2DEditor::dropEvent(QDropEvent* event)
{
  Base::handle_drop(event);
  QWidget::dropEvent(event);
}

void D2DEditor::dragEnterEvent(QDragEnterEvent* event)
{
  Base::handle_drag(event);
  QWidget::dragEnterEvent(event);
}

void D2DEditor::inputMethodEvent(QInputMethodEvent* event)
{
  Base::handle_ime_event(event);
  QWidget::inputMethodEvent(event);
}

void D2DEditor::mouseMoveEvent(QMouseEvent* event)
{
  QWidget::mouseMoveEvent(event);
  Base::handle_mouse_move(event);
}

D2DEditor::OffscreenRenderingPair
D2DEditor::create_render_target(u32 width, u32 height)
{
  auto size = D2D1::SizeU(width, height);
  ComPtr<ID2D1DeviceContext> target;
  ComPtr<ID2D1Bitmap1> bitmap;
  device->CreateDeviceContext(
    D2D1_DEVICE_CONTEXT_OPTIONS_ENABLE_MULTITHREADED_OPTIMIZATIONS,
    &target
  );
  target->CreateBitmap(
    size, nullptr, 0,
    D2D1::BitmapProperties1(
      D2D1_BITMAP_OPTIONS_TARGET,
      D2D1::PixelFormat(
        DXGI_FORMAT_B8G8R8A8_UNORM,
        D2D1_ALPHA_MODE_PREMULTIPLIED
      )
    ),
    &bitmap
  );
  target->SetTarget(bitmap.Get());
  target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
  target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
  target->SetDpi(default_dpi, default_dpi);
  return {target, bitmap};
}

void D2DEditor::paintEvent(QPaintEvent*)
{
  device_context->BeginDraw();
  auto [cols, rows] = nvim_dimensions();
  auto [font_width, font_height] = font_dimensions();
  auto bg = D2D1::ColorF(default_bg().to_uint32());
  ComPtr<ID2D1SolidColorBrush> bg_brush = nullptr;
  device_context->CreateSolidColorBrush(bg, &bg_brush);
  auto r = D2D1::RectF(0, 0, width(), height());
  auto grid_clip_rect = D2D1::RectF(0, 0, cols * font_width, rows * font_height);
  device_context->FillRectangle(r, bg_brush.Get());
  device_context->PushAxisAlignedClip(grid_clip_rect, D2D1_ANTIALIAS_MODE_ALIASED);
  for(auto& grid_base : grids)
  {
    auto* grid = static_cast<D2DPaintGrid2*>(grid_base.get());
    if (!grid->hidden)
    {
      grid->process_events();
      grid->render(device_context.Get());
    }
  }
  device_context->PopAxisAlignedClip();
  if (!n_cursor.hidden() && cmdline->hidden())
  {
    auto* grid = find_grid(n_cursor.grid_num());
    if (grid && !grid->hidden)
    {
      auto* d2dgrid = static_cast<D2DPaintGrid2*>(grid);
      d2dgrid->draw_cursor(device_context.Get(), n_cursor);
    }
  }
  device_context->EndDraw();
}

void D2DEditor::keyPressEvent(QKeyEvent* event)
{
  Base::handle_key_press(event);
}

void D2DEditor::focusInEvent(QFocusEvent* event)
{
  Base::handle_focusgained(event);
  QWidget::focusInEvent(event);
}

void D2DEditor::focusOutEvent(QFocusEvent* event)
{
  Base::handle_focuslost(event);
  QWidget::focusOutEvent(event);
}

std::unique_ptr<PopupMenu> D2DEditor::popup_new()
{
  return std::make_unique<PopupMenuQ>(&hl_state, this);
}

std::unique_ptr<Cmdline> D2DEditor::cmdline_new()
{
  return std::make_unique<CmdlineQ>(hl_state, &n_cursor, this);
}

void D2DEditor::setup()
{
  Base::setup();
  listen_for_notification("NVUI_VSYNC", paramify<bool>([this](bool sync) {
    if (sync == vsync) return;
    set_vsync(sync);
  }));
  listen_for_notification("NVUI_TOGGLE_VSYNC", [this](const auto&) {
    set_vsync(!vsync);
  });
  nvim->exec_viml(R"(
    command! -nargs=1 NvuiVsync call rpcnotify(g:nvui_rpc_chan, "NVUI_VSYNC", <args>)
    command! NvuiToggleVsync call rpcnotify(g:nvui_rpc_chan, "NVUI_TOGGLE_VSYNC")
  )");
}

void D2DEditor::set_vsync(bool sync)
{
  auto hwnd = (HWND) winId();
  auto size = D2D1::SizeU(width(), height());
  auto properties = D2D1::HwndRenderTargetProperties(hwnd, size);
  if (!sync)
  {
    // This disables vsync
    properties.presentOptions = D2D1_PRESENT_OPTIONS_IMMEDIATELY;
  }
  device_context = nullptr;
  d2d_factory->CreateHwndRenderTarget(
    D2D1::RenderTargetProperties(),
    properties,
    hwnd_target.ReleaseAndGetAddressOf()
  );
  hwnd_target->QueryInterface(device_context.ReleaseAndGetAddressOf());
  device_context->GetDevice(device.ReleaseAndGetAddressOf());
  device_context->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
  device_context->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
  device_context->SetDpi(default_dpi, default_dpi);
  vsync = sync;
  emit render_targets_updated();
  update();
}

void D2DEditor::redraw() { update(); }

void D2DEditor::set_fonts(std::span<FontDesc> fontlist)
{
  if (fontlist.empty()) return;
  dw_formats.clear();
  dw_fonts.clear();
  fallback_indices.clear();
  for(auto it = fontlist.rbegin(); it != fontlist.rend(); ++it)
  {
    if (it->point_size > 0) current_point_size = it->point_size;
  }
  ComPtr<IDWriteFontCollection> font_collection = nullptr;
  dw_factory->GetSystemFontCollection(&font_collection);
  if (!font_collection)
  {
    nvim->err_write("Could not get system font collection.\n");
    return;
  }
  for(const auto& fdesc : fontlist)
  {
    const auto& name = fdesc.name;
    auto wcname = QString::fromStdString(name).toStdWString();
    auto font = font_from_name(wcname, font_collection.Get());
    auto format = format_from_name(
      wcname, current_point_size, default_dpi, dw_factory.Get()
    );
    if (!font || !format)
    {
      nvim->err_write(fmt::format("Could not load font '{}'.\n", name));
      continue;
    }
    dw_formats.emplace_back(std::move(format));
    dw_fonts.emplace_back(std::move(font));
  }
  // Make sure there's always at least one format and font
  if (dw_formats.empty())
  {
    static const auto default_font_name = default_font_family().toStdWString();
    dw_fonts.push_back(font_from_name(default_font_name, font_collection.Get()));
    dw_formats.push_back(format_from_name(
      default_font_name, current_point_size, default_dpi, dw_factory.Get()
    ));
  }
  update_font_metrics();
}

void D2DEditor::linespace_changed(float)
{
  update_font_metrics();
}

void D2DEditor::charspace_changed(float)
{
  update_font_metrics();
}

static DWRITE_HIT_TEST_METRICS metrics_for(
  std::wstring_view text,
  DWriteFactory* factory,
  IDWriteTextFormat* text_format
)
{
  ComPtr<IDWriteTextLayout> tl = nullptr;
  factory->CreateTextLayout(
    text.data(),
    (UINT32) text.size(),
    text_format,
    std::numeric_limits<float>::max(),
    std::numeric_limits<float>::max(),
    &tl
  );
  DWRITE_HIT_TEST_METRICS ht_metrics;
  float ignore;
  tl->HitTestTextPosition(0, 0, &ignore, &ignore, &ht_metrics);
  return ht_metrics;
}

void D2DEditor::update_font_metrics()
{
  if (dw_formats.empty()) return;
  auto format = dw_formats.front().Get();
  auto metrics = metrics_for(L"W", dw_factory.Get(), format);
  float font_width = metrics.width + charspacing();
  float font_height = std::ceil(metrics.height + linespacing());
  set_font_dimensions(font_width, font_height);
  auto* popup = static_cast<PopupMenuQ*>(popup_menu.get());
  auto firstnamelength = format->GetFontFamilyNameLength();
  std::wstring name;
  name.resize(firstnamelength + 1);
  format->GetFontFamilyName(name.data(), (UINT32) name.size());
  QFont f;
  f.setFamily(QString::fromWCharArray(name.c_str()));
  f.setPointSizeF(current_point_size);
  f.setLetterSpacing(QFont::AbsoluteSpacing, charspace);
  popup->font_changed(f, font_dimensions());
  emit font_changed();
}

void D2DEditor::create_grid(u32 x, u32 y, u32 w, u32 h, u64 id)
{
  grids.emplace_back(std::make_unique<D2DPaintGrid2>(this, x, y, w, h, id));
}

u32 D2DEditor::font_for_ucs(u32 ucs)
{
  if (ucs < 256) return 0;
  auto it = fallback_indices.find(ucs);
  if (it != fallback_indices.end()) return it->second;
  auto index = calc_fallback_index(ucs);
  fallback_indices[ucs] = index;
  return index;
}

u32 D2DEditor::calc_fallback_index(u32 ucs)
{
  for(u32 i = 0; i < dw_fonts.size(); ++i)
  {
    BOOL has = false;
    dw_fonts[i]->HasCharacter(ucs, &has);
    if (has) return i;
  }
  return 0;
}

DWriteFactory* D2DEditor::dwrite_factory()
{
  return dw_factory.Get();
}
