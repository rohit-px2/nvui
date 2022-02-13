#ifndef NVUI_GUI_HPP
#define NVUI_GUI_HPP

#include <QObject>
#include <QMainWindow>
#include <QWidget>
#include <QFont>
#include <QStackedWidget>
#include <QToolBar>
#include <QLayout>
#include "decide_renderer.hpp"
#include "nvim.hpp"
#include "qeditor.hpp"
#include "titlebar.hpp"
#include "hlstate.hpp"
#include <iostream>
#include <memory>
#include <span>
#include <msgpack.hpp>
#include <QEvent>
#include <unordered_map>
#include <QSemaphore>

class Window;

#if defined(Q_OS_WIN)
#include "platform/windows/d2deditor.hpp"
#endif

constexpr int tolerance = 10; //10px tolerance for resizing

/// The main window class which holds the rest of the GUI components.
/// Fundamentally, the Neovim area is just 1 big text box.
/// However, there are additional features that we are trying to
/// support.
class Window : public QMainWindow
{
  Q_OBJECT
#if defined(USE_DIRECT2D)
  using EditorType = D2DEditor;
#elif defined(USE_QPAINTER)
  using EditorType = QEditor;
#endif
public:
  Window(
    std::string nvim_path,
    std::vector<std::string> nvim_args,
    std::unordered_map<std::string, bool> capabilities,
    int width,
    int height,
    bool size_set,
    bool custom_titlebar,
    QWidget* parent = nullptr
  );
public slots:
  void resize_or_move(const QPointF& p);
  /**
   * Returns whether the window is frameless or not
   */
  inline bool is_frameless() const
  {
    return windowFlags() & Qt::FramelessWindowHint;
  }

  /**
   * Maximize the window.
   */
  void maximize();
  /**
   * Connects to the signals emitted
   * by the editor.
   * These signals get emitted by its UISignaller.
   */
  void connect_editor_signals(EditorType&);
private:
  /// Save current window state and settings.
  void save_state();
  /// Load saved config state, if it exists.
  /// Returns true if any values were set.
  bool load_config();
  /**
   * Disable the frameless window.
   * The window should be in frameless mode,
   * so windowState() & Qt::FramelessWindowHint should be true.
   */
  void disable_frameless_window();
  /**
   * Enable the frameless window.
   * The window should not be in frameless window mode,
   * so windowState() & Qt::FramelessWindowHint should be false.
   */
  void enable_frameless_window();
  /**
   * Hides the title bar.
   * This just always hides it,
   * since there is no reason not to, unlike with showing.
   */
  inline void hide_title_bar()
  {
    title_bar->hide();
  }
  
  /**
   * Shows the titlebar. Only activates if the window is a
   * frameless window (otherwise you would get two title bars,
   * the OS one and the custom one).
   */
  inline void show_title_bar()
  {
    if (!is_frameless()) return;
    title_bar->show();
  }

  /**
   * If fullscreen is true, shows the window in fullscreen mode,
   * otherwise reverts back to how it was before.
   */
  void set_fullscreen(bool fullscreen);
  /**
   * Turn the window fullscreen.
   * If the window is already fullscreen, this does nothing.
   */
  void fullscreen();
  /**
   * Un-fullscreen the window, restoring its original state.
   */
  void un_fullscreen();
  /**
   * Updates the title bar colors.
   * If titlebar_colors contains a value, then it uses
   * titlebar_colors's values.
   * Otherwise, it uses the default colors.
   */
  void update_titlebar_colors(QColor fg, QColor bg);
  void remove_editor(EditorType* editor);
  void make_active_editor(int index);
  void create_editor(
    int width, int height, std::string nvim_path, std::vector<std::string> args,
    std::unordered_map<std::string, bool> capabilities
  );
  void select_editor_from_dialog();
  QtEditorUIBase& current_editor();
  void update_default_colors(QColor fg, QColor bg);
  bool resizing;
  bool maximized = false;
  bool moving = false;
  std::unique_ptr<TitleBar> title_bar;
  QFlags<Qt::WindowState> prev_state;
  QColor default_fg = Qt::white;
  QColor default_bg = Qt::black;
  template<typename T>
  using opt = std::optional<T>;
  std::pair<opt<QColor>, opt<QColor>> titlebar_colors;
  QStackedWidget* editor_stack;
signals:
  void win_state_changed(Qt::WindowStates new_state);
  void default_colors_changed(QColor fg, QColor bg);
protected:
  bool nativeEvent(const QByteArray& e_type, void* msg, long* result) override;
  void changeEvent(QEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void moveEvent(QMoveEvent* event) override;
  void closeEvent(QCloseEvent* event) override;
};

#endif // NVUI_WINDOW_HPP
