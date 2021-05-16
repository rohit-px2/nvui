#include "window.hpp"
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

constexpr int tolerance = 10; //10px tolerance for resizing

//static void print_rect(const std::string& prefix, const QRect& rect)
//{
  ////std::cout << prefix << "\n(" << rect.x() << ", " << rect.y() << ", " << rect.width() << ", " << rect.height() << ")\n";
//}

Window::Window(QWidget* parent, std::shared_ptr<Nvim> nv)
: QMainWindow(parent),
  resizing(false),
  title_bar(nullptr),
  nvim(nv)
{
  setMouseTracking(true);
  setWindowFlags(Qt::FramelessWindowHint);
  resize(640, 480);
  title_bar = std::make_unique<TitleBar>("nvui", this);
}

// TODO: Improve thread safety.
// Since msgpack::object only makes a shallow copy, if the data is updated
// in the Nvim::read_output function our redraw_args will change, which will
// be bad if we are still reading the data while that happens.
// To counteract this we copy the data but if the data is updated while
// the data is being copied, we will similarly run into an error.
// This could be solved using locks, but since the data is passed through
// a Qt event queue the amount of time that the Nvim thread is paused
// may or may not be small.
// I think the best way would be to create a copy of the data on the
// Nvim thread and pass it to to Qt thread, but msgpack::object_handle
// also cannot be copied so ???
void Window::handle_redraw(msgpack::object redraw_args)
{
  using std::cout;
  const auto oh = msgpack::clone(redraw_args);
  const msgpack::object& obj = oh.get();
  assert(obj.type == msgpack::type::ARRAY);
  const auto& arr = obj.via.array;
  for(std::uint32_t i = 0; i < arr.size; ++i)
  {
    // The params is an array of arrays, we should get
    // an array at index i
    const msgpack::object& o = arr.ptr[i];
    assert(o.type == msgpack::type::ARRAY);
    const auto& task = o.via.array;
    assert(task.size >= 1);
    assert(task.ptr[0].type == msgpack::type::STR);
    const std::string task_name = task.ptr[0].as<std::string>();
    // Get corresponding handler
    const auto func_it = handlers.find(task_name);
    if (func_it != handlers.end())
    {
      // This should only run once in most cases
      // Sometimes, calls like "hl_attr_define" give more than one parameter
      // The 0-th object was the task_name so we skip that
      for(std::uint32_t j = 1; j < task.size; ++j)
      {
        func_it->second(this, task.ptr[j]);
      }
    }
    else
    {
      cout << "No handler found for task " << task_name << '\n';
    }
  }
}

void Window::set_handler(std::string method, obj_ref_cb handler)
{
  handlers[method] = handler;
}

void Window::register_handlers()
{
  // Set GUI handlers before we set the notification handler (since Nvim runs on a different thread,
  // it can be called any time)
  set_handler("hl_attr_define", [](Window* w, const msgpack::object& obj) {
    w->hl_state.define(obj);
  });
  set_handler("hl_group_set", [](Window* w, const msgpack::object& obj) {
    w->hl_state.group_set(obj);
  });
  // The lambda will get invoked on the Nvim::read_output thread, we use
  // invokeMethod to then handle the data on our Qt thread.
  assert(nvim);
  nvim->set_notification_handler("redraw", [this](msgpack::object obj) {
    QMetaObject::invokeMethod(
      this, "handle_redraw", Qt::QueuedConnection, Q_ARG(msgpack::object, obj)
    );
  });
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
    if (handle->startSystemResize(edges)) {}
    else
    {
      std::cout << "Resize didn't work\n";
    }
  }
  else
  {
    if (handle->startSystemMove()) {}
    else
    {
      std::cout << "Move didn't work\n";
    }
  }
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
    setCursor(Qt::ArrowCursor);
  }
}

void Window::mouseMoveEvent(QMouseEvent* event)
{
  const ResizeType type = should_resize(rect(), tolerance, event);
  setCursor(Qt::CursorShape(type));
}
