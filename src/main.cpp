#include <QApplication>
#include <QFileInfo>
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
#include <fmt/format.h>
#include <boost/process/env.hpp>

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
  for(const auto& e : v)
  {
    if (e == "--") return; // Don't process command line arguments beyond "--" (those are for Neovim)
    if (e.rfind(s, 0) == 0) { f(e.substr(s.size())); return; }
  }
}

vector<string> neovim_args(const vector<string>& listofargs)
{
  vector<string> args;
  for(std::size_t i = 0; i < listofargs.size(); ++i)
  {
    if (listofargs[i] == "--")
    {
      for(std::size_t j = i + 1; j < listofargs.size(); ++j)
      {
        args.push_back(listofargs[j]);
      }
      return args;
    }
  }
  return args;
}

vector<string> get_args(int argc, char** argv)
{
  return vector<string>(argv + 1, argv + argc);
}

const std::string geometry_opt = "--geometry=";
int main(int argc, char** argv)
{
#ifdef Q_OS_LINUX
  // See issue #21
  auto env = boost::this_process::environment();
  if (env.find("FONTCONFIG_PATH") == env.end())
  {
    env.set("FONTCONFIG_PATH", "/etc/fonts");
  }
#endif
  const auto args = get_args(argc, argv);
  // Arguments to pass to nvim
  vector<string> nvim_args {"--embed"};
  vector<string> cl_nvim_args = neovim_args(args);
  nvim_args.insert(nvim_args.end(), cl_nvim_args.begin(), cl_nvim_args.end());
  string nvim_path = "";
  std::unordered_map<std::string, bool> capabilities = {
    {"ext_tabline", false},
    {"ext_multigrid", false},
    {"ext_cmdline", false},
    {"ext_popupmenu", false},
    {"ext_linegrid", true},
    {"ext_hlstate", false}
  };
  for(const auto& capability : capabilities)
  {
    // Ex. --ext_popupmenu=true
    on_argument(args, fmt::format("--{}=", capability.first), [&](std::string opt) {
      if (opt == "true") capabilities[capability.first] = true;
      else capabilities[capability.first] = false;
    });
    // Single argument (e.g. --ext_popupmenu)
    on_argument(args, fmt::format("--{}", capability.first), [&](std::string opt) {
      if (opt.size() == 0) capabilities[capability.first] = true;
    });
  }
  on_argument(args, "--nvim=", [&](std::string opt) {
    QFileInfo nvim_path_info {QString::fromStdString(opt)};
    if (nvim_path_info.exists() && nvim_path_info.isExecutable()) nvim_path = opt;
  });
  int width = 100;
  int height = 50;
  std::ios_base::sync_with_stdio(false);
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
    const auto nvim = std::make_shared<Nvim>(nvim_path, nvim_args);
    Window w {nullptr, nvim, width, height};
    w.register_handlers();
    w.show();
    nvim->set_var("nvui", 1);
    nvim->attach_ui(width, height, capabilities);
    nvim->on_exit([&] {
      QMetaObject::invokeMethod(&w, &QMainWindow::close, Qt::QueuedConnection);
    });
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
