#include <QApplication>
#include <QMetaObject>
#include <QMetaType>
#include <string>
#include <vector>
#include <thread>
#include <ios>
#include <iostream>
#include "nvim.hpp"
#include "window.hpp"
#include <msgpack.hpp>
#include <QStyleFactory>
using std::string;
using std::vector;

constexpr int DEFAULT_ROWS = 200;
constexpr int DEFAULT_COLS = 50;

vector<string> get_args(int argc, char** argv)
{
  return vector<string>(argv + 1, argv + argc);
}

Q_DECLARE_METATYPE(msgpack::object)

int main(int argc, char** argv)
{
  qRegisterMetaType<msgpack::object>();
  std::ios_base::sync_with_stdio(false);
  const auto args = get_args(argc, argv);
  QApplication app {argc, argv};
  const auto nvim = std::make_shared<Nvim>();
  Window w {nullptr, nvim};
  w.show();
  w.register_handlers();
  // Register msgpack::object to Qt
  // We have to register msgpack::object
  nvim->set_var("nvui", 1);
  nvim->attach_ui(DEFAULT_ROWS, DEFAULT_COLS);
  return app.exec();
}
