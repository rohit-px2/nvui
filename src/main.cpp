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

template<typename Func>
void do_thing_if_has_opt(const std::vector<string>& v, const std::string& s, const Func& f)
{
  const auto it = std::find_if(v.begin(), v.end(), [s](const auto& e) {
    return e.rfind(s, 0) == 0;
  });
  if (it != v.end())
  {
    f(*it);
  }
}

vector<string> get_args(int argc, char** argv)
{
  return vector<string>(argv + 1, argv + argc);
}

Q_DECLARE_METATYPE(msgpack::object)
Q_DECLARE_METATYPE(msgpack::object_handle*)

const std::string geometry_opt = "--geometry=";
int main(int argc, char** argv)
{
  int width = 100;
  int height = 50;
  std::ios_base::sync_with_stdio(false);
  qRegisterMetaType<msgpack::object>();
  qRegisterMetaType<msgpack::object_handle*>();
  const auto args = get_args(argc, argv);
  // Get "size" option
  do_thing_if_has_opt(args, geometry_opt, [&](std::string size_opt) {
    size_opt = size_opt.substr(geometry_opt.size());
    std::size_t pos = size_opt.find("x");
    if (pos != std::string::npos)
    {
      int new_width = std::stoi(size_opt.substr(0, pos));
      int new_height = std::stoi(size_opt.substr(pos + 1));
      width = new_width;
      height = new_height;
    }
  });
  QApplication app {argc, argv};
  try
  {
    const auto nvim = std::make_shared<Nvim>();
    Window w {nullptr, nvim, width, height};
    w.register_handlers();
    w.show();
    nvim->set_var("nvui", 1);
    nvim->attach_ui(width, height);
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
