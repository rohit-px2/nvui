#include "msgpack_overrides.hpp"
#include "window.hpp"
#include "utils.hpp"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <QObject>
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
  bool custom_titlebar,
  QWidget* parent
)
: QMainWindow(parent),
  resizing(false),
  title_bar(std::make_unique<TitleBar>("nvui", this)),
  editor_area(width, height, std::move(capabilities), nvp, std::move(nva))
{
  editor_area.setup();
  setMouseTracking(true);
  prev_state = windowState();
  const auto [font_width, font_height] = editor_area.font_dimensions();
  resize(width * font_width, height * font_height);
  if (custom_titlebar) enable_frameless_window();
  else title_bar->hide();
  QObject::connect(title_bar.get(), &TitleBar::resize_move, this, &Window::resize_or_move);
  QObject::connect(
    editor_area.ui_signaller(), &UISignaller::default_colors_changed,
    [this](QColor fg, QColor bg) {
      update_titlebar_colors(fg, bg);
    }
  );
  QObject::connect(editor_area.ui_signaller(), &UISignaller::closed,
    [this] { close(); }
  );
  setWindowIcon(QIcon(constants::appicon()));
  // We'll do this later
  setCentralWidget(&editor_area);
  editor_area.setFocus();
  editor_area.attach();
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
  if (editor_area.nvim_exited()) event->accept();
  else
  {
    event->ignore();
    editor_area.confirm_qa();
  }
}
