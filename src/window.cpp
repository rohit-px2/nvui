#include "window.hpp"
#include <iostream>
#include <qobjectdefs.h>

Window::Window(QWidget *parent, std::shared_ptr<Nvim> nv)
: QMainWindow(parent),
  nvim(nv) {}

void Window::handle_redraw(msgpack::object redraw_args)
{
}

void Window::set_handler(std::string method, obj_ref_cb handler)
{
  handlers[method] = handler;
}

void Window::register_handlers()
{
  // Note: We have to use invokeMethod because these are actually going to be
  // run on a separate thread.
  assert(nvim);
  nvim->set_notification_handler("redraw", [&](msgpack::object obj) {
    QMetaObject::invokeMethod(
      this, "handle_redraw", Qt::QueuedConnection, Q_ARG(msgpack::object, obj)
    );
  });
}
