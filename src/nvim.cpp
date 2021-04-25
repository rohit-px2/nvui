#include "nvim.hpp"
#include <boost/asio/windows/stream_handle.hpp>
#include <boost/process/search_path.hpp>
#include <memory>
#include <windows.h>
#include <boost/asio.hpp>
#include <boost/process.hpp>
#include <boost/bind.hpp>
#include <iostream>

#define BOOST_PROCESS_WINDOWS_USE_NAMED_PIPE

namespace bp = boost::process;
void Nvim::read_output(const boost::system::error_code &ec, std::size_t size)
{
  // TODO: Placeholder (need to do something with the information)
  std::cout << &buf << std::endl;
}

Nvim::Nvim()
: ios(), in(), ap(ios)
{
  auto nvim_path = bp::search_path("nvim");
  if (nvim_path == "")
  {
    return;
  }
  nvim = boost::process::child(
    nvim_path,
    "--version",
    boost::process::std_in < in,
    boost::process::std_out > ap,
    ios
  );
  boost::asio::async_read(
    ap,
    buf,
    boost::bind(&Nvim::read_output, this, _1, _2)
  );
  ios.run();
}

static const std::unordered_map<std::string, int> cases {
};
void Nvim::decide(const std::string& msg)
{
}

void Nvim::send_input(std::uint8_t key)
{

}
