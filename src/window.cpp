#include "window.hpp"
#include <iostream>
#include <qobjectdefs.h>

Window::Window(QWidget *parent, std::shared_ptr<Nvim> nv)
: QMainWindow(parent),
  nvim(nv)
{
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
  set_handler("hl_attr_define", [](Window *w, const msgpack::object& obj) {
    w->hl_state.define(obj);
  });
  set_handler("hl_group_set", [](Window *w, const msgpack::object& obj) {
    w->hl_state.group_set(obj);
  });
  // Note: We have to use invokeMethod because these are actually going to be
  // run on a separate thread.
  assert(nvim);
  nvim->set_notification_handler("redraw", [this](msgpack::object obj) {
    QMetaObject::invokeMethod(
      this, "handle_redraw", Qt::QueuedConnection, Q_ARG(msgpack::object, obj)
    );
  });
}
