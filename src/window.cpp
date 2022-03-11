#include "window.hpp"
#include "utils.hpp"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <QObject>
#include <QInputDialog>
#include <QLabel>
#include <QHBoxLayout>
#include <QPushButton>
#include <QStyle>
#include <QApplication>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <QIcon>
#include <QWindow>
#include <QSizeGrip>
#include <sstream>
#include <thread>
#include "constants.hpp"
#include "object.hpp"
#include <QApplication>
#include <unordered_map>
#include "nvim_utils.hpp"
#include "config.hpp"

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
/// Add margin to a HWND window
static void add_margin(HWND hwnd, MARGINS margins)
{
  DwmExtendFrameIntoClientArea(hwnd, &margins);
}
/// Remove margins from a HWND window
static void remove_margin(HWND hwnd)
{
  MARGINS margins {0, 0, 0, 0};
  DwmExtendFrameIntoClientArea(hwnd, &margins);
}
/// Setup the frameless window for aero snap, etc.
static void windows_setup_frameless(HWND hwnd)
{
  add_margin(hwnd, {0, 0, 1, 0});
  SetWindowLong(
    hwnd,
    GWL_STYLE,
    WS_THICKFRAME | WS_MINIMIZEBOX
    | WS_MAXIMIZEBOX | WS_CLIPCHILDREN | WS_CAPTION
  );
}
#endif

using u32 = std::uint32_t;

Window::Window(
  std::string nvp,
  std::vector<std::string> nva,
  std::unordered_map<std::string, bool> capabilities,
  int width,
  int height,
  bool size_set,
  bool custom_titlebar,
  QWidget* parent
)
: QMainWindow(parent),
  resizing(false),
  title_bar(std::make_unique<TitleBar>("nvui", this)),
  editor_stack(new QStackedWidget())
{
  bool loaded = load_config();
  auto* editor_area = new EditorType(
    width, height, std::move(capabilities), std::move(nvp), std::move(nva)
  );
  editor_stack->setMouseTracking(true);
  editor_stack->addWidget(editor_area);
  editor_area->setup();
  setMouseTracking(true);
  prev_state = windowState();
  /// When geometry option isn't set, we use the config geometry
  /// Also, command-line options override config settings
  /// so we do this when the cli option is set.
  if (!loaded || size_set)
  {
    showNormal();
    const auto [font_width, font_height] = editor_area->font_dimensions();
    resize(width * font_width, height * font_height);
  }
  if (custom_titlebar) enable_frameless_window();
  else title_bar->hide();
  QObject::connect(title_bar.get(), &TitleBar::resize_move, this, &Window::resize_or_move);
  setWindowIcon(QIcon(constants::appicon()));
  connect_editor_signals(*editor_area);
  editor_area->setFocus();
  editor_area->attach();
  setCentralWidget(editor_stack);
  editor_stack->setCurrentIndex(0);
}

QtEditorUIBase& Window::current_editor()
{
  return *static_cast<EditorType*>(editor_stack->currentWidget());
}

void Window::update_default_colors(QColor fg, QColor bg)
{
  default_fg = fg;
  default_bg = bg;
  update_titlebar_colors(fg, bg);
}

void Window::connect_editor_signals(EditorType& editor)
{
  using namespace std;
  auto* signaller = editor.ui_signaller();
  connect(
    signaller, &UISignaller::default_colors_changed, this,
    [this](QColor fg, QColor bg) {
      update_default_colors(fg, bg);
    }
  );
  connect(signaller, &UISignaller::closed, this,
    [this, p_editor = &editor] {
      remove_editor(p_editor);
    }
  );
  connect(signaller, &UISignaller::editor_spawned, this,
    [this](string nvp, unordered_map<string, bool> capabilities, vector<string> args) {
      auto nvim_dims = current_editor().nvim_dimensions();
      create_editor(
        nvim_dims.width, nvim_dims.height,
        nvp, std::move(args), std::move(capabilities)
      );
  });
  connect(signaller, &UISignaller::editor_switched, this, [this](std::size_t idx) {
    make_active_editor(idx);
  });
  connect(signaller, &UISignaller::fullscreen_set, this, [this](bool b) {
    set_fullscreen(b);
  });
  connect(signaller, &UISignaller::fullscreen_toggled, this, [this] {
    if (isFullScreen()) set_fullscreen(false);
    else set_fullscreen(true);
  });
  connect(signaller, &UISignaller::frame_set, this, [this](bool frame) {
    if (frame) disable_frameless_window();
    else enable_frameless_window();
  });
  connect(signaller, &UISignaller::frame_toggled, this, [this] {
    if (is_frameless()) disable_frameless_window();
    else enable_frameless_window();
  });
  connect(signaller, &UISignaller::titlebar_set, this, [this](bool tb) {
    if (tb) { enable_frameless_window(); title_bar->show(); }
    else disable_frameless_window();
  });
  connect(signaller, &UISignaller::titlebar_toggled, this, [this] {
    if (is_frameless()) disable_frameless_window();
    else enable_frameless_window();
  });
  connect(signaller, &UISignaller::window_opacity_changed, this, [this](double opa) {
    setWindowOpacity(opa);
  });
  connect(signaller, &UISignaller::titlebar_font_family_set, this, [this](QString f) {
    title_bar->set_font_family(f);
  });
  connect(signaller, &UISignaller::titlebar_font_size_set, this, [this](double ps) {
    title_bar->set_font_size(ps);
  });
  connect(signaller, &UISignaller::title_changed, this, [this](QString title) {
    title_bar->set_title_text(title);
  });
  connect(signaller, &UISignaller::titlebar_fg_set, this, [this](QColor fg) {
    titlebar_colors.first = fg;
    update_titlebar_colors(default_fg, default_bg);
  });
  connect(signaller, &UISignaller::titlebar_bg_set, this, [this](QColor bg) {
    titlebar_colors.second = bg;
    update_titlebar_colors(default_fg, default_bg);
  });
  connect(signaller, &UISignaller::titlebar_colors_unset, this, [this] {
    titlebar_colors.first.reset();
    titlebar_colors.second.reset();
    update_titlebar_colors(default_fg, default_bg);
  });
  connect(signaller, &UISignaller::titlebar_fg_bg_set, this,
    [this](QColor fg, QColor bg) {
      titlebar_colors.first = fg;
      titlebar_colors.second = bg;
      update_titlebar_colors(default_fg, default_bg);
  });
  connect(signaller, &UISignaller::editor_changed_previous, this, [this] {
    if (editor_stack->count() == 1) return;
    int active = editor_stack->currentIndex();
    if (active == 0) active = editor_stack->count() - 1;
    else active -= 1;
    make_active_editor(active);
  });
  connect(signaller, &UISignaller::editor_changed_next, this, [this] {
    if (editor_stack->count() == 1) return;
    int active = editor_stack->currentIndex();
    if (active == editor_stack->count() - 1) active = 0;
    else active += 1;
    make_active_editor(active);
  });
  connect(signaller, &UISignaller::editor_selection_list_opened, this, [this] {
    select_editor_from_dialog();
  });
}

void Window::select_editor_from_dialog()
{
  QStringList items;
  for(int i = 0; i < editor_stack->count(); ++i)
  {
    auto* editor = static_cast<EditorType*>(editor_stack->widget(i));
    QString cwd = QString::fromStdString(editor->current_dir());
    items.append(QString("%1 - %2").arg(i).arg(cwd));
  }
  QInputDialog dialog;
  dialog.setComboBoxItems(items);
  dialog.setWindowTitle(tr("Editor Selection"));
  dialog.setLabelText(tr("Choose the current editor instance to display."));
  dialog.exec();
  if (dialog.result() == QDialog::Rejected) return;
  int selected = dialog.comboBoxItems().indexOf(dialog.textValue());
  if (selected < 0 || selected >= editor_stack->count()) return;
  make_active_editor(selected);
}

void Window::remove_editor(EditorType* editor)
{
  editor_stack->removeWidget(editor);
  if (editor_stack->count() == 0) close();
  else
  {
    make_active_editor(0);
  }
}

void Window::make_active_editor(int index)
{
  if (index >= editor_stack->count()) return;
  editor_stack->setCurrentIndex(index);
  editor_stack->currentWidget()->setFocus();
  auto& editor = current_editor();
  update_default_colors(editor.default_fg().qcolor(), editor.default_bg().qcolor());
}

void Window::create_editor(
  int width, int height, std::string nvim_path, std::vector<std::string> args,
  std::unordered_map<std::string, bool> capabilities
)
{
  EditorType* editor = nullptr;
  try
  {
    editor = new EditorType(
      width, height, std::move(capabilities), nvim_path, std::move(args)
    );
  }
  catch(...)
  {
    return;
  }
  editor->setup();
  connect_editor_signals(*editor);
  editor->attach();
  int index = editor_stack->addWidget(editor);
  make_active_editor(index);
}

enum ResizeType
{
  NoResize = Qt::ArrowCursor,
  Horizontal = Qt::SizeHorCursor,
  Vertical = Qt::SizeVerCursor,
  NorthEast = Qt::SizeBDiagCursor,
  SouthWest = Qt::SizeFDiagCursor
};

static ResizeType should_resize(
  const QRect& win_rect,
  const int tolerance,
  const QMouseEvent* event
)
{
  const QRect inner_rect = QRect(
    win_rect.x() + tolerance, win_rect.y() + tolerance,
    win_rect.width() - 2 * tolerance, win_rect.height() - 2 * tolerance
  );
  const auto& pos = event->pos();
  if (inner_rect.contains(pos))
  {
    return ResizeType::NoResize;
  }
  const int width = win_rect.width();
  const int height = win_rect.height();
  const int mouse_x = pos.x();
  const int mouse_y = pos.y();
  if (mouse_x > tolerance && mouse_x < (width - tolerance))
  {
    return ResizeType::Vertical;
  }
  else if (mouse_y > tolerance && mouse_y < (height - tolerance))
  {
    return ResizeType::Horizontal;
  }
  // Top left
  else if ((mouse_x <= tolerance && mouse_y <= tolerance))
  {
    return ResizeType::SouthWest;
  }
  // Bot right
  else if (mouse_x >= (width - tolerance)
      && mouse_y >= (height - tolerance))
  {
    return ResizeType::SouthWest;
  }
  // Bot left
  else if (mouse_x <= tolerance && mouse_y >= (height - tolerance))
  {
    return ResizeType::NorthEast;
  }
  // Should only activate for top right
  else
  {
    return ResizeType::NorthEast;
  }
}

void Window::resize_or_move(const QPointF& p)
{
  Qt::Edges edges;
  if (p.x() > width() - tolerance)
  {
    edges |= Qt::RightEdge;
  }
  if (p.x() < tolerance)
  {
    edges |= Qt::LeftEdge;
  }
  if (p.y() < tolerance)
  {
    edges |= Qt::TopEdge;
  }
  if (p.y() > height() - tolerance)
  {
    edges |= Qt::BottomEdge;
  }
  QWindow* handle = windowHandle();
  if (edges != 0 && !isMaximized())
  {
    if (handle->startSystemResize(edges)) {
    }
    else
    {
      std::cout << "Resize didn't work\n";
    }
  }
  else
  {
    if (handle->startSystemMove()) {
    }
    else
    {
      std::cout << "Move didn't work\n";
    }
  }
  title_bar->update_maxicon();
}

void Window::mousePressEvent(QMouseEvent* event)
{
  if (is_frameless())
  {
    resize_or_move(event->localPos());
  }
  else
  {
    QMainWindow::mousePressEvent(event);
  }
}

void Window::mouseReleaseEvent(QMouseEvent* event)
{
  Q_UNUSED(event) // at least, for now
}

void Window::mouseMoveEvent(QMouseEvent* event)
{
  if (isMaximized())
  {
    // No resizing
    return;
  }
  if (is_frameless())
  {
    const ResizeType type = should_resize(rect(), tolerance, event);
    setCursor(Qt::CursorShape(type));
  }
  else
  {
    setCursor(Qt::ArrowCursor);
    QMainWindow::mouseMoveEvent(event);
  }
}

void Window::resizeEvent(QResizeEvent* event)
{
  title_bar->update_maxicon();
  QMainWindow::resizeEvent(event);
}

void Window::moveEvent(QMoveEvent* event)
{
#ifdef Q_OS_WIN
  static QScreen* cur_screen = nullptr;
  if (!is_frameless()) return;
  if (!cur_screen) cur_screen = screen();
  else if (cur_screen != screen())
  {
    cur_screen = screen();
    SetWindowPos((HWND) winId(), nullptr, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                   SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
    return;
  }
#endif
  title_bar->update_maxicon();
  QMainWindow::moveEvent(event);
}

void Window::disable_frameless_window()
{
  auto flags = windowFlags();
  if (!(flags & Qt::FramelessWindowHint)) /* Already disabled */ return;
  if (title_bar) title_bar->hide();
  flags &= ~Qt::FramelessWindowHint;
#ifdef Q_OS_WIN
  remove_margin((HWND) winId());
#endif
  setWindowFlags(flags);
  showNormal();
}

void Window::enable_frameless_window()
{
  auto flags = windowFlags();
  if (flags & Qt::FramelessWindowHint) /* Already enabled */ return;
  if (title_bar) title_bar->show();
  flags |= Qt::FramelessWindowHint;
  setWindowFlags(flags);
#ifdef Q_OS_WIN
  windows_setup_frameless((HWND) winId());
#endif
  // Kick the window out of any maximized/fullscreen state.
  showNormal();
}

void Window::set_fullscreen(bool enable_fullscreen)
{
  if (isFullScreen() == enable_fullscreen) return;
  if (enable_fullscreen) fullscreen();
  else
  {
    un_fullscreen();
  }
}

void Window::changeEvent(QEvent* event)
{
  if (event->type() == QEvent::WindowStateChange)
  {
    emit win_state_changed(windowState());
    auto ev = static_cast<QWindowStateChangeEvent*>(event);
    prev_state = ev->oldState();
  }
#ifdef Q_OS_WIN
    if ((windowState() & Qt::WindowMaximized) && is_frameless())
    {
      // 8px bigger on each side when maximized on Windows as a frameless
      // window
      setContentsMargins(8, 8, 8, 8);
    }
    else
    {
      setContentsMargins(0, 0, 0, 0);
    }
#endif
  QMainWindow::changeEvent(event);
}

bool Window::nativeEvent(const QByteArray& e_type, void* msg, long* result)
{
  Q_UNUSED(e_type);
#ifdef Q_OS_WIN
  if (!is_frameless()) return false;
  MSG* message = static_cast<MSG*>(msg);
  switch(message->message)
  {
    case WM_NCCALCSIZE:
      *result = 0;
      return true;
  }
#else
  Q_UNUSED(msg);
  Q_UNUSED(result);
#endif
  return false;
}

void Window::maximize()
{
  setWindowState(Qt::WindowMaximized);
  setGeometry(screen()->availableGeometry());
}

void Window::fullscreen()
{
  if (isFullScreen()) return;
  title_bar->hide();
#ifdef Q_OS_WIN
  if (is_frameless())
  {
    // Remove margins (otherwise you see a border in fullscreen mode)
    remove_margin((HWND) winId());
  }
#endif
  showFullScreen();
}

void Window::un_fullscreen()
{
  if (!isFullScreen()) return;
#ifdef Q_OS_WIN
  if (is_frameless())
  {
    // Set the margins back to get the aero effects
    add_margin((HWND) winId(), {0, 0, 1, 0});
  }
#endif
  if (prev_state & Qt::WindowMaximized) maximize();
  else showNormal();
  show_title_bar();
}

void Window::update_titlebar_colors(QColor fg, QColor bg)
{
  auto foreground = titlebar_colors.first.value_or(fg);
  auto background = titlebar_colors.second.value_or(bg);
  title_bar->colors_changed(foreground, background);
}

void Window::closeEvent(QCloseEvent* event)
{
  // This first case should never happen since
  // the window should automatically close once all the editors
  // are closed
  if (editor_stack->count() <= 0)
  {
    save_state();
    close();
  }
  else
  {
    event->ignore();
    for(int i = 0; i < editor_stack->count(); ++i)
    {
      static_cast<EditorType*>(editor_stack->widget(i))->confirm_qa();
    }
  }
}

void Window::save_state()
{
  Config::set("window/geometry", geometry());
  Config::set("window/frameless", is_frameless());
  Config::set("window/fullscreen", isFullScreen());
  Config::set("window/maximized", isMaximized());
}

bool Window::load_config()
{
  bool set = false;
  if (auto geom = Config::get("window/geometry"); geom.canConvert<QRect>())
  {
    setGeometry(geom.value<QRect>());
    set = true;
  }
  if (auto fs = Config::get("window/fullscreen"); fs.canConvert<bool>())
  {
    set_fullscreen(fs.toBool());
    set = true;
  }
  if (auto maxim = Config::get("window/maximized"); maxim.canConvert<bool>())
  {
    showMaximized();
    set = true;
  }
  if (auto frameless = Config::get("window/frameless"); frameless.canConvert<bool>())
  {
    frameless.toBool() ? enable_frameless_window() : disable_frameless_window();
    set = true;
  }
  return set;
}
