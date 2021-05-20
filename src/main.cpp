#include <QApplication>
#include <QMetaObject>
#include <QMetaType>
#include <QMessageBox>
#include <QStyleFactory>
#include <QStringBuilder>
#include <string>
#include <vector>
#include <thread>
#include <ios>
#include <iostream>
#include "nvim.hpp"
#include "window.hpp"
#include <msgpack.hpp>

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
  //const auto args = get_args(argc, argv);
  QApplication app {argc, argv};
  try
  {
    const auto nvim = std::make_shared<Nvim>();
    Window w {nullptr, nvim};
    w.register_handlers();
    w.show();
    nvim->set_var("nvui", 1);
    nvim->attach_ui(DEFAULT_ROWS, DEFAULT_COLS);
    return app.exec();
  }
  catch (const std::exception& e)
  {
    // The main purpose is to catch the exception where Neovim is not
    // found in PATH
    QMessageBox msg;
    msg.setText("Error occurred: " % QLatin1String(e.what()) % ".");
    msg.exec();
  }
}
