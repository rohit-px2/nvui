#include <boost/asio/buffers_iterator.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/process/search_path.hpp>
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

std::vector<string> get_args(int argc, char **argv)
{
  return std::vector<string>(argv + 1, argv + argc);
}

int main(int argc, char **argv)
{
  auto nvim = std::make_shared<Nvim>();
}