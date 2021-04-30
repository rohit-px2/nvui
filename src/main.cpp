#include <boost/asio/buffers_iterator.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/process/search_path.hpp>
#include <cstdlib>
#include <ios>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include "nvim.hpp"
#include <boost/process.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>
#include <array>
using string = std::string;

constexpr int DEFAULT_ROWS = 200;
constexpr int DEFAULT_COLS = 50;
std::vector<string> get_args(int argc, char **argv)
{
  return std::vector<string>(argv + 1, argv + argc);
}

int main(int argc, char **argv)
{
  std::ios_base::sync_with_stdio(false);
  auto nvim = std::make_shared<Nvim>();
  nvim->attach_ui(DEFAULT_ROWS, DEFAULT_COLS);
  while(nvim->running())
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }
  std::cout << "Closing..." << std::endl;
  std::cout << "Process exited with exit code " << nvim->exit_code() << std::endl;
  std::exit(nvim->exit_code());
}
