#ifndef NVUI_QEDITOR_HPP
#define NVUI_QEDITOR_HPP

#include "qt_editorui_base.hpp"
#include <QWidget>

class Font;

class QEditor : public QWidget, public QtEditorUIBase
{
  Q_OBJECT
  using Base = QtEditorUIBase;
public:
  QEditor(
    int cols,
    int rows,
    std::unordered_map<std::string, bool> capabilities,
    std::string nvim_path,
    std::vector<std::string> nvim_args,
    QWidget* parent = nullptr
  );
  ~QEditor() override;
  void setup() override;
  const auto& fallback_list() const { return fonts; }
  const auto& main_font() const { return first_font; }
  u32 font_for_ucs(u32 ucs);
signals:
  void font_changed();
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
  void focusInEvent(QFocusEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;
  bool focusNextPrevChild(bool) override { return false; }
private:
  u32 calc_fallback_index(u32 ucs);
  std::unique_ptr<PopupMenu> popup_new() override;
  std::unique_ptr<Cmdline> cmdline_new() override;
  void redraw() override;
  void create_grid(u32 x, u32 y, u32 w, u32 h, u64 id) override;
  void set_fonts(std::span<FontDesc> fonts) override;
private:
  std::unordered_map<u32, u32> fallback_indices;
  void update_font_metrics();
  QFont first_font;
  std::vector<Font> fonts;
  double device_pixelratio;
};

#endif // NVUI_QEDITOR_HPP
