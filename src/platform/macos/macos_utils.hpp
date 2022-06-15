#ifndef NVUI_PLATFORM_MACOS_UTILS_HPP
#define NVUI_PLATFORM_MACOS_UTILS_HPP

#include <QtCore>

namespace macos_utils
{

inline void set_env_vars()
{
  QProcess env_printer;
  auto shell = qgetenv("SHELL");
  if (shell.isEmpty()) shell = "/bin/bash";
  // Ported from Goneovim's editor.go at
  // https://github.com/akiyosi/goneovim/blob/981f41440935542ed35b3ef93cfdcd17744a4e1a/editor/editor.go#L548
  // When nvui is compiled into a .app file and run, there are differences
  // between the environment when it is run by clicking vs. running from command line.
  // When run by clicking the PATH doesn't contain the Neovim executable path
  // even if it was set by the user.
  // Thus we have to print out the path in an external process and set it ourselves.
  env_printer.start(shell, {"-lc", "env", "-i"});
  if (!env_printer.waitForFinished(5000))
  {
    return;
  }
  auto out = env_printer.readAllStandardOutput().split('\n');
  for(const auto& line : out)
  {
    // The name of the environment variable, and then its value.
    // Separated by an '=' sign.
    auto s = line.split('=');
    if (s.size() < 2) continue;
    qputenv(s[0], s[1]);
  }
}

} // namespace macos_utils

#endif // NVUI_PLATFORM_MACOS_UTILS_HPP
