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
#include <QIcon>
#include <QWindow>
#include <QSizeGrip>
#include <sstream>
#include <thread>

/// Default is just for logging purposes.
//constexpr auto default_handler = [](Window* w, const msgpack::object& obj) {
  //std::cout << obj << '\n';
//};

//static void print_rect(const std::string& prefix, const QRect& rect)
//{
  ////std::cout << prefix << "\n(" << rect.x() << ", " << rect.y() << ", " << rect.width() << ", " << rect.height() << ")\n";
//}

static constexpr const char* func_name(const QLatin1String full_name)
{
  // Can't use QString/std::string at compile time in c++17
  if (full_name.startsWith(QLatin1String("Window::")))
  {
    const char* s = full_name.latin1();
    return (QLatin1String(s + 8, full_name.size() - 8)).latin1();
  }
  else
  {
    return full_name.latin1();
  }
}

#define FUNCNAME(func) (void(&func), func_name(QLatin1String(#func)))

Window::Window(QWidget* parent, std::shared_ptr<Nvim> nv, int width, int height)
: QMainWindow(parent),
  semaphore(1),
  resizing(false),
  title_bar(nullptr),
  hl_state(),
  nvim(nv.get()),
  editor_area(nullptr, &hl_state, nvim)
{
  setAttribute(Qt::WA_OpaquePaintEvent);
  assert(width > 0 && height > 0);
  setMouseTracking(true);
  QObject::connect(this, &Window::resize_done, &editor_area, &decltype(editor_area)::resized);
  setWindowFlags(Qt::FramelessWindowHint);
  const auto font_dims = editor_area.font_dimensions();
  resize(width * std::get<0>(font_dims), height * std::get<1>(font_dims));
  emit resize_done(size());
  title_bar = std::make_unique<TitleBar>("nvui", this);
  setWindowIcon(QIcon("../assets/appicon.png"));
  title_bar->set_separator(" • ");
  // We'll do this later
  setCentralWidget(&editor_area);
  editor_area.setFocus();
}

void Window::handle_redraw(msgpack::object_handle* redraw_args)
{
  using std::cout;
#ifndef NDEBUG
  using Clock = std::chrono::high_resolution_clock;
  const auto start = Clock::now();
#endif
  const auto oh = safe_copy(redraw_args);
  const msgpack::object& obj = oh.get();
  assert(obj.type == msgpack::type::ARRAY);
  const auto& arr = obj.via.array.ptr[2].via.array;
  for(std::uint32_t i = 0; i < arr.size; ++i)
  {
    // The params is an array of arrays, we should get
    // an array at index i
    msgpack::object& o = arr.ptr[i];
    assert(o.type == msgpack::type::ARRAY);
    const auto& task = o.via.array;
    assert(task.size >= 1);
    assert(task.ptr[0].type == msgpack::type::STR);
    std::string task_name = task.ptr[0].as<std::string>();
    // Get corresponding handler
    const auto func_it = handlers.find(task_name);
    if (func_it != handlers.end())
    {
      func_it->second(this, task.ptr + 1, task.size - 1);
      //for(std::uint32_t j = 1; j < task.size; ++j)
      //{
        //func_it->second(this, arr.ptr[j]);
      //}
    }
    else
    {
      cout << "No handler found for task " << task_name << '\n';
    }
  }
#ifndef NDEBUG
  const auto end = Clock::now();
  std::cout << "Took " << std::chrono::duration<double, std::milli>(end - start).count() << " ms.\n";
#endif
}

void Window::handle_bufenter(msgpack::object_handle* bufe_args)
{
  const auto oh = safe_copy(bufe_args);
  const auto& obj = oh.get();
  assert(obj.type == msgpack::type::ARRAY);
  const auto& arr = obj.via.array.ptr[2].via.array;
  assert(arr.size == 1);
  const msgpack::object& file_obj = arr.ptr[0];
  assert(file_obj.type == msgpack::type::STR);
  //QString&& file_name = QString::fromStdString(file_obj.as<std::string>());
  QString&& file_name = file_obj.as<QString>();
  title_bar->set_right_text(file_name);
}

void Window::dirchanged_titlebar(msgpack::object_handle* dir_args)
{
  const auto oh = safe_copy(dir_args);
  const auto& obj = oh.get();
  const auto& arr = obj.via.array.ptr[2].via.array;
  assert(arr.size == 2); // Local dir name, and full path (we might use later)
  assert(arr.ptr[0].type == msgpack::type::STR);
  //QString&& new_dir = QString::fromStdString(arr.ptr[0].as<std::string>());
  QString&& new_dir = arr.ptr[0].as<QString>();
  //assert(arr.ptr[1].type == msgpack::type::STR);
  //const QString full_dir = QString::fromStdString(arr.ptr[1].as<std::string>());
  title_bar->set_middle_text(new_dir);
}

void Window::set_handler(std::string method, obj_ref_cb handler)
{
  handlers[method] = handler;
}

msgpack_callback Window::sem_block(msgpack_callback func)
{
  return [this, func](msgpack::object_handle* obj) {
    semaphore.acquire();
    func(obj);
    semaphore.acquire();
    semaphore.release();
  };
}

msgpack::object_handle Window::safe_copy(msgpack::object_handle* obj)
{
  msgpack::object_handle oh {obj->get(), std::move(obj->zone())};
  semaphore.release();
  return oh;
}

void Window::register_handlers()
{
  // Set GUI handlers before we set the notification handler (since Nvim runs on a different thread,
  // it can be called any time)
  set_handler("hl_attr_define", [](Window* w, const msgpack::object* obj, std::uint32_t size) {
    for(std::uint32_t i = 0; i < size; ++i)
    {
      w->hl_state.define(obj[i]);
    }
  });
  set_handler("hl_group_set", [](Window* w, const msgpack::object* obj, std::uint32_t size) {
    for(std::uint32_t i = 0; i < size; ++i)
    {
      w->hl_state.group_set(obj[i]);
    }
  });
  set_handler("default_colors_set", [](Window* w, const msgpack::object* obj, std::uint32_t size) {
    for(std::uint32_t i = 0; i < size; ++i)
    {
      w->hl_state.default_colors_set(obj[i]);
    }
    const HLAttr& def_clrs = w->hl_state.default_colors_get();
    const Color& fgc = def_clrs.foreground;
    const Color& bgc = def_clrs.background;
    QColor fg {fgc.r, fgc.g, fgc.b};
    QColor bg {bgc.r, bgc.g, bgc.b};
    w->title_bar->set_color(fg, bg.lightness() > 127 ? bg.darker(110) : bg.lighter(120));
  });
  set_handler("grid_line", [](Window* w, const msgpack::object* obj, std::uint32_t size) {
    w->editor_area.grid_line(obj, size);
  });
  set_handler("option_set", [](Window* w, const msgpack::object* obj, std::uint32_t size) {
    w->editor_area.option_set(obj, size);
  });
  set_handler("grid_resize", [](Window* w, const msgpack::object* obj, std::uint32_t size) {
    w->editor_area.grid_resize(obj, size);
  });
  set_handler("flush", [](Window* w, const msgpack::object* obj, std::uint32_t size) {
    w->editor_area.flush();
  });
  set_handler("win_pos", [](Window* w, const msgpack::object* obj, std::uint32_t size) {
    w->editor_area.win_pos(obj);
  });
  set_handler("grid_clear", [](Window* w, const msgpack::object* obj, std::uint32_t size) {
    w->editor_area.grid_clear(obj, size);
  });
  set_handler("grid_cursor_goto", [](Window* w, const msgpack::object* obj, std::uint32_t size) {
    w->editor_area.grid_cursor_goto(obj, size);
  });
  // The lambda will get invoked on the Nvim::read_output thread, we use
  // invokeMethod to then handle the data on our Qt thread.
  assert(nvim);
  nvim->set_notification_handler("redraw", sem_block([this](msgpack::object_handle* obj) {
    QMetaObject::invokeMethod(
      this, FUNCNAME(Window::handle_redraw), Qt::QueuedConnection, Q_ARG(msgpack::object_handle*, obj)
    );
  }));
  nvim->set_notification_handler("NVUI_BUFENTER", sem_block([this](msgpack::object_handle* obj) {
    QMetaObject::invokeMethod(
      this, FUNCNAME(Window::handle_bufenter), Qt::QueuedConnection, Q_ARG(msgpack::object_handle*, obj)
    );
  }));
  nvim->set_notification_handler("NVUI_DIRCHANGED", sem_block([this](msgpack::object_handle* obj) {
    QMetaObject::invokeMethod(
      this, FUNCNAME(Window::dirchanged_titlebar), Qt::QueuedConnection, Q_ARG(msgpack::object_handle*, obj)
    );
  }));
  // Display current file in titlebar 
  nvim->command("autocmd BufEnter * call rpcnotify(1, 'NVUI_BUFENTER', expand('%:t'))");
  // Display current dir / update file tree on directory change
  nvim->command("autocmd DirChanged * call rpcnotify(1, 'NVUI_DIRCHANGED', fnamemodify(getcwd(), ':t'), getcwd())");
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
  //std::cout << "\nx: " << pos.x() << ", y: " << pos.y() << '\n';
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
  if (edges != 0)
  {
    if (handle->startSystemResize(edges)) {
      resizing = true;
      editor_area.set_resizing(true);
    }
    else
    {
      std::cout << "Resize didn't work\n";
      editor_area.setUpdatesEnabled(true);
    }
  }
  else
  {
    editor_area.setUpdatesEnabled(false);
    if (handle->startSystemMove()) {
      moving = true;
    }
    else
    {
      std::cout << "Move didn't work\n";
      editor_area.setUpdatesEnabled(true);
    }
  }
  title_bar->update_maxicon();
}

void Window::mousePressEvent(QMouseEvent* event)
{
  resize_or_move(event->localPos());
}

void Window::mouseReleaseEvent(QMouseEvent* event)
{
  Q_UNUSED(event) // at least, for now
  if (resizing)
  {
    resizing = false;
    editor_area.set_resizing(false);
    emit resize_done(size());
  }
  if (moving)
  {
    moving = false;
    // setUpdatesEnabled calls update(), but we want to ignore
    editor_area.ignore_next_paint_event();
    editor_area.setUpdatesEnabled(true);
  }
}

void Window::mouseMoveEvent(QMouseEvent* event)
{
  if (isMaximized())
  {
    // No resizing
    return;
  }
  const ResizeType type = should_resize(rect(), tolerance, event);
  setCursor(Qt::CursorShape(type));
}

void Window::resizeEvent(QResizeEvent* event)
{
  if (isMaximized())
  {
    maximized = true;
    emit resize_done(size());
  }
  else if (maximized)
  {
    maximized = false;
    emit resize_done(size());
  }
  title_bar->update_maxicon();
  QMainWindow::resizeEvent(event);
}

void Window::moveEvent(QMoveEvent* event)
{
  title_bar->update_maxicon();
  QMainWindow::moveEvent(event);
}
