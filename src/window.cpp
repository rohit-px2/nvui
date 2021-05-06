#include "window.hpp"
#include <iostream>
#include <sstream>
#include <QDebug>

Window::Window(QWidget *parent, std::shared_ptr<Nvim> nv)
: QMainWindow(parent),
  nvim(nv) {}

void Window::handle_redraw(msgpack::object redraw_args)
{
  std::stringstream ss;
  ss << redraw_args;
  qDebug() << "Redraw: " << ss.str().data() << '\n';
}
