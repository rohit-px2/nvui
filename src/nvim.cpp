#include "nvim.hpp"
#include <bitset>
#include <boost/asio/buffer.hpp>
#include <boost/asio/execution/context.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/system_context.hpp>
#include <boost/asio/windows/stream_handle.hpp>
#include <boost/process/search_path.hpp>
#include <boost/thread/win32/thread_primitives.hpp>
#include <boost/winapi/access_rights.hpp>
#include <boost/winapi/file_management.hpp>
#include <chrono>
#include <cstdlib>
#include <fileapi.h>
#include <initializer_list>
#include <ios>
#include <limits.h>
#include <memory>
#include <mutex>
#include <ostream>
#include <sstream>
#include <thread>
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
//namespace ba = boost::asio;
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

constexpr const auto one_ms = std::chrono::milliseconds(1);
static const std::string RequestNames[] = {
  "nvim_get_api_info",
  "nvim_input",
  "nvim_input_mouse",
  "nvim_eval",
  "nvim_command"
};

// Override packing of std::string to pack as binary string (like Neovim wants)
namespace msgpack {
  namespace adaptor {
    template<>
    struct pack<std::string> {
      template<typename Stream>
      msgpack::packer<Stream>& operator()(msgpack::packer<Stream>& o, std::string const& v) const {
        o.pack_bin(v.size());
        o.pack_bin_body(v.data(), v.size());
        return o;
      }
    };
  }
}

constexpr const int MPACK_MAX_SIZE = 4096;
// ###################### DONE SETTING UP ##################################

static inline std::uint32_t to_uint(char ch)
{
  return static_cast<std::uint32_t>(static_cast<unsigned char>(ch));
}

std::mutex m;
template<typename T>
void Nvim::send_request(const std::string& method, const T& params, int size)
{
  using Packer = msgpack::packer<msgpack::sbuffer>;
  std::lock_guard<std::mutex> lock {m};
  const std::uint64_t msg_type = Type::Request;
  msgpack::sbuffer sbuf;
  const auto msg = std::make_tuple(msg_type, current_msgid, method, params);
  msgpack::pack(sbuf, msg);
  //std::cout << ss.str() << std::endl;
  //std::cout << std::hex;
  const char *d = sbuf.data();
  //std::cout << std::hex;
  for(int i = 0; i < sbuf.size(); i++)
  {
    const char c = d[i];
    std::cout << "0x" << to_uint(c) << ", ";
  }
  const auto oh = msgpack::unpack(sbuf.data(), sbuf.size());
#ifdef _WIN32
  DWORD bytes_written;
  DWORD bytes_to_write = static_cast<DWORD>(sbuf.size());
  bool success = WriteFile(stdin_pipe.native_sink(), (void *)sbuf.data(), bytes_to_write, &bytes_written, nullptr);
  //WriteFile(stdin_pipe.native_source(), (void *)sbuf.data(), bytes_to_write, &bytes_written, nullptr);
  assert(success);
  std::cout << "Bytes written: " << bytes_written << std::endl;
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  ++current_msgid;
#endif
}

template<typename T>
void Nvim::send_notification(const std::string& method, const T& params)
{
  std::lock_guard<std::mutex> lock {m};
  const int msg_type = Type::Notification;
  msgpack::sbuffer sbuf;
  const auto msg = std::make_tuple(msg_type, method, params);
  msgpack::pack(sbuf, msg);
  //std::cout << ss.str() << std::endl;
  //std::cout << std::hex;
  const char *d = sbuf.data();
  //std::cout << std::hex;
  for(int i = 0; i < sbuf.size(); i++)
  {
    const char c = d[i];
    std::cout << "0x" << to_uint(c) << ", ";
  }
  std::cout << "Buffer size: " << sbuf.size() << std::endl;
#ifdef _WIN32
  DWORD bytes_written;
  DWORD bytes_to_write = static_cast<DWORD>(sbuf.size());
  bool success = WriteFile(stdin_pipe.native_sink(), (void *)sbuf.data(), bytes_to_write, &bytes_written, nullptr);
  assert(success);
  std::cout << "Bytes written: " << bytes_written << std::endl;
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  ++current_msgid;
#endif
}

static const std::unordered_map<std::string, bool> capabilities {
  {"ext_linegrid", true},
  {"ext_popupmenu", true}, 
  {"ext_cmdline", true},
  {"ext_hlstate", true}
};

void Nvim::read_output_sync()
{
  bool message_not_complete;
  msgpack::object_handle oh;
  std::cout << std::dec;
  constexpr const int buffer_maxsize = 1024 * 1024;
#ifdef _WIN32
  // On Windows we can use Readfile to get the underlying data, working with ipstream
  // has not been going well.
  std::unique_ptr<char[]> buffer(new char[buffer_maxsize]);
  DWORD bytes_written;
  while(true)
  {
    size_t bytes_read = ReadFile(
      output.pipe().native_source(),
      buffer.get(),
      buffer_maxsize,
      &bytes_written,
      nullptr
    );
    if (bytes_read)
    {
      //std::cout << "Data: " << s << std::endl;
      try
      {
        oh = msgpack::unpack(buffer.get(), bytes_written);
        std::cout << "Unpacked: " << oh.get() << std::endl;
      } catch(const std::exception& e)
      {
        std::cout << "Error occurred: " << e.what() << std::endl;
      }
    }
    else
    {
      // Wait for a bit
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
  }
#endif
  std::cout << "Output closed." << std::endl;
}

void Nvim::attach_ui(const int rows, const int cols)
{
  std::cout << "Attaching UI. Please wait..." << std::endl;
  const auto params = std::make_tuple(rows, cols, capabilities);
  send_notification("nvim_ui_attach", params);
  std::cout << "All Done!" << std::endl;
}

void Nvim::read_error_sync()
{
  std::string line;
  while(std::getline(error, line))
  {
    std::cout << "ERROR:\n" << line << "\n" << std::endl;
  }
}

int Nvim::exit_code()
{
  if (nvim.running())
  {
    return INT_MIN;
  }
  return nvim.exit_code();
}

Nvim::Nvim()
: write(),
  output(),
  error(),
  proc_group(),
  stdin_pipe(),
  stdout_pipe(),
  current_msgid(0),
  num_responses(0)
{
  // Everything is going to get detached because we're keeping the process running,
  // the message processing thread running so that it doesn't get destroyed.
  //read_buffer.reserve(MAX_MSG_SIZE);
  err_reader = std::thread(boost::bind(&Nvim::read_error_sync, this));
  err_reader.detach();
  out_reader = std::thread(boost::bind(&Nvim::read_output_sync, this));
  out_reader.detach();
  auto nvim_path = bp::search_path("nvim");
  nvim = bp::child(
    "nvim --embed",
    bp::std_out > output,
    bp::std_in < stdin_pipe,
    proc_group
  );
  nvim.detach();
  proc_group.detach();
  send_notification("nvim_ui_attach", std::make_tuple(200, 50, capabilities));
  send_request("nvim_eval", std::make_tuple("stdpath('config')"));
  //const auto default_params = std::make_tuple(200, 50, capabilities);
  //send_notification("nvim_ui_attach", default_params);
}

bool Nvim::nvim_running()
{
  return nvim.running();
}

Nvim::~Nvim()
{
  // Close I/O Pipes, join threads, delete anything that is remaining
  nvim.terminate();
  error.pipe().close();
  output.pipe().close();
  write.pipe().close();
  stdout_pipe.close();
  stdin_pipe.close();
}
