#include "nvim.hpp"
#include <boost/asio/buffer.hpp>
#include <boost/asio/execution/context.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/system_context.hpp>
#include <boost/asio/windows/stream_handle.hpp>
#include <boost/process/search_path.hpp>
#include <boost/thread/win32/thread_primitives.hpp>
#include <boost/winapi/access_rights.hpp>
#include <chrono>
#include <initializer_list>
#include <memory>
#include <sstream>
#include <tuple>
#include <windows.h>
#include <boost/asio.hpp>
#include <boost/process.hpp>
#include <boost/bind.hpp>
#include <iostream>
#include <boost/thread.hpp>
#include <msgpack.hpp>
#define BOOST_PROCESS_WINDOWS_USE_NAMED_PIPE
#include <cassert>

namespace bp = boost::process;
namespace ba = boost::asio;
// ######################## SETTING UP ####################################
// constexpr const std::initializer_list<char> EMPTY_LIST {};
const std::vector<int> EMPTY_LIST {};
enum Notifications : std::uint8_t {
  nvim_ui_attach = 0,
  nvim_try_resize = 1,
  nvim_set_var = 2
};

static const std::string NotificationNames[] = {
  "nvim_ui_attach", "nvim_try_resize", "nvim_set_var"
};

enum Request : std::uint8_t {
  nvim_get_api_info = 0,
  nvim_input = 1,
  nvim_input_mouse = 2,
  nvim_eval = 3,
  nvim_command = 4 
};

constexpr const char *RequestNames[] {
  "nvim_get_api_info",
  "nvim_input",
  "nvim_input_mouse",
  "nvim_eval",
  "nvim_command"
};

constexpr const int MPACK_MAX_SIZE = 4096;
// ###################### DONE SETTING UP ##################################

template<typename T>
void Nvim::send_request(const std::string& method, const T& params)
{
  const int msg_type = Type::Request;
  auto msg = std::make_tuple(msg_type, current_msgid, method, params);
  std::stringstream ss;
  msgpack::pack(ss, msg);
  //std::string s = ss.str();
  std::cout << ss.str() << std::endl;
  //std::cout << s << std::endl;
  auto oh = msgpack::unpack(ss.str().data(), ss.str().size());
  std::cout << "Unpacked: " << oh.get() << std::endl;
  //MessageBoxA(NULL, s.c_str(), "msgptest", 0);
  ++current_msgid;
  // Maybe this works?
  write << ss.str() << std::endl;
}

template<typename T>
void Nvim::send_notification(const std::string& method, const T& params)
{
  // When we send a message it should only be a request or a notification.
  const int msg_type = Type::Notification;
  auto msg = std::make_tuple(msg_type, method, params);
  std::stringstream ss;
  msgpack::pack(ss, msg);
  std::string s(ss.str());
  std::cout << ss.str() << std::endl;
  write << ss.str() << std::endl;
}

static const std::unordered_map<std::string, bool> capabilities {
  {"ext_linegrid", true},
  {"ext_popupmenu", true}, 
  {"ext_cmdline", true}
};


void Nvim::read_output(boost::process::async_pipe& p, boost::asio::mutable_buffer& buf)
{
  p.async_read_some(
    buf,
    [&](const boost::system::error_code& ec, const std::size_t size)
    {
      std::cout << "Received " << size << " bytes (" << ec.message() << ")";
      std::cout.write(boost::asio::buffer_cast<const char *>(buf), size) << std::endl;
      if (!ec)
      {
        read_output(p, buf);
      }
    }
  );
}

void Nvim::attach_ui(const int rows, const int cols)
{
  std::cout << "Attaching UI. Please wait..." << std::endl;
  auto params = std::make_tuple(rows, cols, capabilities);
  send_notification("nvim_ui_attach", params);
  std::cout << "All Done!" << std::endl;
}

Nvim::Nvim()
: ios(), write(), read(ios), current_msgid(0), output()
{
  read_buffer.reserve(MAX_MSG_SIZE);
  reader = std::thread([&] {
    std::string line;
    while(std::getline(output, line))
    {
      auto oh = msgpack::unpack(line.data(), line.size());
      std::cout << "NEW LINE:\n" << oh.get() << "\n" << std::endl;
      //std::cout << line << std::endl;
    }
  });
  auto nvim_path = bp::search_path("nvim");
  nvim = bp::child(
    nvim_path,
    "--embed",
    bp::std_out > output,
    bp::std_in < write
  );
  try
  {
    //send_request("nvim_get_api_info", EMPTY_LIST);
    send_request("nvim_eval", std::vector<std::string>({"stdpath('config')"}));
  }
  catch(std::exception const& e)
  {
    std::cout << "An error occurred: " << e.what() << std::endl;
  }
}

bool Nvim::nvim_running()
{
  return nvim.running();
}

Nvim::~Nvim()
{
  // Close I/O Pipes, join threads, delete anything that is remaining
  nvim.terminate();
  output.pipe().close();
  reader.join();
}
