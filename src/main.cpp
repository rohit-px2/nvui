#include <string>
#include <vector>
#include <thread>
#include <ios>
#include <iostream>
#include "nvim.hpp"
using std::string;
using std::vector;

constexpr int DEFAULT_ROWS = 200;
constexpr int DEFAULT_COLS = 50;

vector<string> get_args(int argc, char **argv)
{
  return vector<string>(argv + 1, argv + argc);
}

int main(int argc, char **argv)
{
  std::ios_base::sync_with_stdio(false);
  const auto args = get_args(argc, argv);
  const auto nvim = std::make_shared<Nvim>();
  nvim->attach_ui(DEFAULT_ROWS, DEFAULT_COLS);
  while(nvim->running())
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }
  std::cout << "Closing..." << std::endl;
  std::cout << "Process exited with exit code " << nvim->exit_code() << std::endl;
}
