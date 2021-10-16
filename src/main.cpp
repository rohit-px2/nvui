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
#include <iostream>
#include "nvim.hpp"
#include "window.hpp"
#include <msgpack.hpp>
#include <fmt/format.h>
#include <boost/process/env.hpp>
#include <QProcess>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include "log.hpp"

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

void start_detached(int argc, char** argv)
{
  if (argc < 1)
  {
    LOG_ERROR("No arguments given, could not start in detached mode\n");
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
  int width, height;
  auto data = geom.data();
  auto res = std::from_chars(data, data + pos, width);
  res = std::from_chars(data + pos + 1, data + geom.size(), height);
  return std::pair {width, height};
}

bool is_executable(std::string_view path)
{
  QFileInfo file_info {QString::fromStdString(std::string(path))};
  return file_info.exists() && file_info.isExecutable();
}

void log_to_file(const std::string& fname, std::string level = "")
{
  using namespace spdlog;
  auto logger = basic_logger_mt("nvui_log", fname);
  set_default_logger(logger);
  level::level_enum logging_level = level::info;
  if (!level.empty()) logging_level = level::from_str(level);
  // Don't let it be turned off
  if (logging_level == level::off) logging_level = level::info;
  set_level(logging_level);
  info("Starting with a logging level of: {}", level::to_string_view(logging_level));
}

void show_message_box(QString msg, QString title = "")
{
  QMessageBox mbox;
  mbox.setText(msg);
  mbox.setWindowTitle(title);
  mbox.exec();
}

Q_DECLARE_METATYPE(msgpack::object)
Q_DECLARE_METATYPE(msgpack::object_handle*)

int main(int argc, char** argv)
{
  spdlog::set_level(spdlog::level::err);
  qRegisterMetaType<msgpack::object>();
  qRegisterMetaType<msgpack::object_handle*>();
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
  if (auto lvl = get_arg(args, "--log"))
  {
    if (!lvl->empty() && lvl->rfind('=') == 0)
    {
      *lvl = lvl->substr(1);
    }
    log_to_file("nvui_log.txt", std::string(*lvl));
  }
  auto titlebar = get_arg(args, "--titlebar");
  if (titlebar)
  {
    if (titlebar->empty()) custom_titlebar = true;
    else custom_titlebar = *titlebar == "=true" ? true : false;
  }
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
  for(const auto& capability : capabilities)
  {
    // Ex. --ext_popupmenu=true
    auto set_arg = get_arg(args, fmt::format("--{}=", capability.first));
    if (set_arg)
    {
      capabilities[capability.first] = *set_arg == "true" ? true : false;
    }
    // Single argument (e.g. --ext_popupmenu)
    auto arg = get_arg(args, fmt::format("--{}", capability.first));
    if (arg && arg->empty()) capabilities[capability.first] = true;
  }
  std::ios_base::sync_with_stdio(false);
  QApplication app {argc, argv};
  Nvim nvim;
  try
  {
    nvim.open_local(nvim_path, nvim_args);
  }
  catch(const std::exception& e)
  {
    show_message_box(e.what(), "error");
    LOG_ERROR("Error occurred: {}", e.what());
    return EXIT_FAILURE;
  }
  Window w(nullptr, &nvim, width, height, custom_titlebar);
  w.register_handlers();
  w.show();
  nvim.set_var("nvui", 1);
  nvim.attach_ui(width, height, capabilities);
  nvim.on_exit([&] {
    LOG_INFO("Nvim shut down. Closing nvui...");
    QMetaObject::invokeMethod(&w, &QMainWindow::close, Qt::QueuedConnection);
  });
  return app.exec();
}
