#include <QApplication>
#include <QFileInfo>
#include <QMetaObject>
#include <QMetaType>
#include <QMessageBox>
#include <QStyleFactory>
#include <QStringBuilder>
#include <charconv>
#include <string>
#include <string_view>
#include <vector>
#include <thread>
#include <ios>
#include <fstream>
#include <iostream>
#include "nvim.hpp"
#include "window.hpp"
#include <msgpack.hpp>
#include <fmt/format.h>
#include <boost/process/env.hpp>
#include <QProcess>

using std::string;
using std::vector;

std::optional<std::string_view> get_arg(
  const std::vector<std::string>& args,
  const std::string_view prefix
)
{
  for(const auto& arg : args)
  {
    if (arg == "--") return {};
    if (arg.rfind(prefix, 0) == 0)
    {
      return std::string_view(arg).substr(prefix.size());
    }
  }
  return std::nullopt;
}

bool extract_arg_bool(
  const vector<string>& args, const std::string_view prefix,
  bool defaultval, bool notfound
)
{
  if (auto arg = get_arg(args, fmt::format("{}=", prefix)))
  {
    return arg.value() == "true" ? true : false;
  }
  if (auto arg = get_arg(args, prefix); arg && arg->empty())
  {
    return defaultval;
  }
  return notfound;
}

vector<string> neovim_args(const vector<string>& listofargs)
{
  vector<string> args;
  for(std::size_t i = 0; i < listofargs.size(); ++i)
  {
    const auto& arg = listofargs[i];
    if (arg == "--")
    {
      for(std::size_t j = i + 1; j < listofargs.size(); ++j)
      {
        args.push_back(listofargs[j]);
      }
      return args;
    }
    /// Add it to the list if it's a filename that actually exists.
    /// Doesn't work for anything that starts with "--"
    else if (std::ifstream f {arg}; f && !arg.starts_with("--"))
    {
      args.push_back(arg);
    }
  }
  return args;
}

vector<string> get_args(int argc, char** argv)
{
  return vector<string>(argv + 1, argv + argc);
}

void start_detached(int argc, char** argv)
{
  if (argc < 1)
  {
    fmt::print("No arguments given, could not start in detached mode\n");
    return;
  }
  QString prog_name = argv[0];
  QStringList args;
  for(int i = 1; i < argc; ++i)
  {
    QString arg = argv[i];
    /// Don't spawn processes infinitely
    if (arg != "--detached") args.append(arg);
  }
  QProcess p;
  p.setProgram(prog_name);
  p.setArguments(args);
  p.startDetached();
}

std::optional<std::pair<int, int>> parse_geometry(std::string_view geom)
{
  auto pos = geom.find('x');
  if (pos == std::string::npos) return std::nullopt;
  int width=0, height=0;
  auto data = geom.data();
  auto res = std::from_chars(data, data + pos, width);
  if (res.ec != std::errc()) return {};
  res = std::from_chars(data + pos + 1, data + geom.size(), height);
  if (res.ec != std::errc()) return {};
  return std::pair {width, height};
}

bool is_executable(std::string_view path)
{
  QFileInfo file_info {QString::fromStdString(std::string(path))};
  return file_info.exists() && file_info.isExecutable();
}

int main(int argc, char** argv)
{
  const auto args = get_args(argc, argv);
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
  bool custom_titlebar = false;
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
  auto should_detach = get_arg(args, "--detached");
  if (should_detach)
  {
    start_detached(argc, argv);
    return 0;
  }
  custom_titlebar = extract_arg_bool(args, "--titlebar", true, false);
  auto path_to_nvim = get_arg(args, "--nvim=");
  if (path_to_nvim && is_executable(*path_to_nvim))
  {
    nvim_path = std::string(*path_to_nvim);
  }
  auto geometry = get_arg(args, "--geometry=");
  if (geometry)
  {
    auto parsed = parse_geometry(*geometry);
    if (parsed) std::tie(width, height) = *parsed;
  }
  for(auto& [key, val]: capabilities)
  {
    bool preset = val;
    val = extract_arg_bool(args, fmt::format("--{}", key), true, preset);
  }
  std::optional<std::pair<int, int>> window_size;
  auto winsize = get_arg(args, "--size=");
  if (winsize)
  {
    window_size = parse_geometry(winsize.value());
  }
  std::ios_base::sync_with_stdio(false);
  QApplication app {argc, argv};
  Window w {nvim_path, nvim_args, capabilities, width, height, custom_titlebar};
  w.show();
  return app.exec();
}
