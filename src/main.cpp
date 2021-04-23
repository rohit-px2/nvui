#include <iostream>
#include <string>
#include <vector>
#include <windows.h>
#include "nvim.hpp"

using string = std::string;

std::vector<string> getArgs(int argc, char **argv)
{
  return std::vector<string>(argv + 1, argv + argc);
}

int main(int argc, char **argv)
{
  auto v = getArgs(argc, argv);
}
