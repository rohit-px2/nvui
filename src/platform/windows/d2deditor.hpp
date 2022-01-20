#ifndef NVUI_D2DEDITOR_HPP
#define NVUI_D2DEDITOR_HPP

#include "qt_editorui_base.hpp"
#include "textformat.hpp"

#ifndef Q_OS_WIN
#error "D2DEditor requires Windows."
#endif // Q_OS_WIN

#include <wrl/client.h>
#include <d2d1.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <dwrite_1.h>

class D2DEditor : public QWidget, public QtEditorUIBase
{
  Q_OBJECT
  using Base = QtEditorUIBase;
  template<typename T>
  using ComPtr = Microsoft::WRL::ComPtr<T>;
  // Same structure as 'Font' in font.hpp in terms of storing fonts
  // but for IDWriteTextFormat
public:
  D2DEditor(
    int cols,
    int rows,
    std::unordered_map<std::string, bool> capabilities,
    std::string nvim_path,
    std::vector<std::string> nvim_args,
    QWidget* parent = nullptr,
    bool vsync = true
  );
  void setup() override;
  QPaintEngine* paintEngine() const override { return nullptr; }
  u32 font_for_ucs(u32 ucs);
  // Creates a bitmap render target with the given width and height.
  struct OffscreenRenderingPair
  {
    ComPtr<ID2D1DeviceContext> target;
    ComPtr<ID2D1Bitmap1> bitmap;
  };
  OffscreenRenderingPair create_render_target(u32 width, u32 height);
  IDWriteFactory* dwrite_factory();
  const auto& fallback_list() const { return dw_formats; }
  ~D2DEditor() override;
signals:
  void layouts_invalidated();
  void render_targets_updated();
protected:
  void linespace_changed(float new_ls) override;
  void charspace_changed(float new_cs) override;
protected:
  void resizeEvent(QResizeEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void dropEvent(QDropEvent* event) override;
  void dragEnterEvent(QDragEnterEvent* event) override;
  void inputMethodEvent(QInputMethodEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void paintEvent(QPaintEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  bool focusNextPrevChild(bool) override { return false; }
  void focusInEvent(QFocusEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;
private:
  void set_vsync(bool);
  void update_font_metrics();
  std::unique_ptr<PopupMenu> popup_new() override;
  std::unique_ptr<Cmdline> cmdline_new() override;
  void redraw() override;
  void create_grid(u32 x, u32 y, u32 w, u32 h, u64 id) override;
  void set_fonts(std::span<FontDesc> fonts) override;
  u32 calc_fallback_index(u32 ucs);
private:
  std::unordered_map<u32, u32> fallback_indices {};
  std::vector<ComPtr<IDWriteFont>> dw_fonts;
  std::vector<TextFormat> dw_formats;
  ComPtr<IDWriteFactory> dw_factory = nullptr;
  ComPtr<ID2D1Factory> d2d_factory = nullptr;
  ComPtr<ID2D1DeviceContext> device_context = nullptr;
  ComPtr<ID2D1HwndRenderTarget> hwnd_target = nullptr;
  ComPtr<ID2D1Device> device = nullptr;
  float current_point_size = 12.0f;
  bool vsync = true;
};

#endif // NVUI_D2DEDITOR_HPP
