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

/**
 * When a string is observed in v that starts with s, this function trims s
 * from the beginning of the string and calls f with the resulting string.
 * This only occurs for the first string that satisfies the condition.
 */
template<typename Func>
void on_argument(const std::vector<string>& v, const std::string& s, const Func& f)
{
  const auto it = std::find_if(v.begin(), v.end(), [s](const auto& e) {
    return e.rfind(s, 0) == 0;
  });
  if (it != v.end())
  {
    std::string a = it->substr(s.size());
    f(std::move(a));
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
  const auto args = get_args(argc, argv);
  std::unordered_map<std::string, bool> capabilities = {
    {"ext_tabline", false},
    {"ext_multigrid", false},
    {"ext_cmdline", false},
    {"ext_popupmenu", false},
    {"ext_linegrid", true}
  };
  on_argument(args, "--ext-popupmenu=", [&](std::string opt) {
    if (opt == "true") capabilities["ext_popupmenu"] = true;
    else capabilities["ext_popupmenu"] = false;
  });
  on_argument(args, "--ext-cmdline=", [&](std::string opt) {
    if (opt == "true") capabilities["ext_cmdline"] = true;
    else capabilities["ext_cmdline"] = false;
  });
  int width = 100;
  int height = 50;
  std::ios_base::sync_with_stdio(false);
  qRegisterMetaType<msgpack::object>();
  qRegisterMetaType<msgpack::object_handle*>();
  // Get "size" option
  on_argument(args, geometry_opt, [&](std::string size_opt) {
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
    nvim->attach_ui(width, height, capabilities);
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
