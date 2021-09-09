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

std::optional<string> get_arg(
  const vector<string>& args,
  const std::string& prefix
)
{
  for(const auto& arg : args)
  {
    if (arg == "--") break;
    if (arg.rfind(prefix, 0) == 0) return arg.substr(prefix.size());
  }
  return std::nullopt;
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

Q_DECLARE_METATYPE(msgpack::object)
Q_DECLARE_METATYPE(msgpack::object_handle*)

static const ClientInfo client_info {
  "nvui",
  {
    {"major", 0},
    {"minor", 1}
  },
  "ui",
  {},
  {
    {"website", "https://github.com/rohit-px2/nvui"},
    {"license", "MIT"}
  }
};

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
  int width = 100;
  int height = 50;
  const auto args = get_args(argc, argv);
  const auto nvim_path = get_arg(args, "--nvim=").value_or("");
  const auto tcp = get_arg(args, "--tcp=");
  const auto geometry_opt = get_arg(args, "--geometry=");
  if (geometry_opt)
  {
    auto pos = geometry_opt->find("x");
    if (pos != std::string::npos)
    {
      width = std::stoi(geometry_opt->substr(0, pos));
      height = std::stoi(geometry_opt->substr(pos + 1));
    }
  }
  // Arguments to pass to nvim
  vector<string> nvim_args {"--embed"};
  vector<string> cl_nvim_args = neovim_args(args);
  nvim_args.insert(nvim_args.end(), cl_nvim_args.begin(), cl_nvim_args.end());
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
    using fmt::format;
    // Ex. --ext_popupmenu=true
    const auto cap_set = get_arg(args, format("--{}=", capability.first));
    if (cap_set)
    {
      capabilities[capability.first] = *cap_set == "true" ? true : false;
    }
    // Single argument (e.g. --ext_popupmenu)
    const auto cap_no_eq = get_arg(args, format("--{}", capability.first));
    if (cap_no_eq) capabilities[capability.first] = true;
  }
  std::ios_base::sync_with_stdio(false);
  qRegisterMetaType<msgpack::object>();
  qRegisterMetaType<msgpack::object_handle*>();
  QApplication app {argc, argv};
  Nvim nvim;
  try
  {
    if (tcp) nvim.open_tcp(*tcp);
    else nvim.open_local(nvim_path, std::move(nvim_args));
  }
  catch(const std::exception& e)
  {
    QMessageBox msg;
    msg.setText(QString::fromStdString(e.what()));
    msg.exec();
    return EXIT_FAILURE;
  }
  nvim.set_client_info(client_info);
  nvim.set_var("nvui", 1);
  Window w {nullptr, &nvim, width, height};
  w.register_handlers();
  nvim.attach_ui(width, height, capabilities);
  w.show();
  nvim.on_exit([&] {
    QMetaObject::invokeMethod(&w, &QMainWindow::close, Qt::QueuedConnection);
  });
  return app.exec();
}
