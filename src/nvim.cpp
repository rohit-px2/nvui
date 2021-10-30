#include <exception>
#include <mutex>
#define BOOST_PROCESS_WINDOWS_USE_NAMED_PIPE
#include "nvim.hpp"
#include <fstream>
#include <iostream>
#include <vector>
#include <cstdint>
#include <chrono>
#include <string>
#include <thread>
#include <tuple>
#include <boost/process.hpp>
#include <algorithm>
#include <fmt/core.h>
#include <fmt/format.h>
#include <QtCore>
#include "object.hpp"

#ifdef _WIN32
#include <boost/process/windows.hpp>
#endif

namespace bp = boost::process;

// ######################## SETTING UP ####################################

using Lock = std::lock_guard<std::mutex>;

//constexpr auto one_ms = std::chrono::milliseconds(1);

// ###################### DONE SETTING UP ##################################

//// Useful for logging char data
//static inline std::uint32_t to_uint(char ch)
//{
  //return static_cast<std::uint32_t>(static_cast<unsigned char>(ch));
//}
/// Constructor
Nvim::Nvim(std::string path, std::vector<std::string> args)
: notification_handlers(),
  request_handlers(),
  closed(false),
  num_responses(0),
  current_msgid(0),
  proc_group(),
  stdout_pipe(),
  stdin_pipe(),
  error()
{
  auto nvim_path = boost::filesystem::path(path);
  if (path.empty())
  {
    nvim_path = bp::search_path("nvim");
    if (nvim_path.empty())
    {
      throw std::runtime_error("Neovim not found in PATH");
    }
  }
  nvim = bp::child(
    nvim_path,
    args,
    bp::std_out > stdout_pipe,
    bp::std_in < stdin_pipe,
    bp::std_err > error,
    proc_group
#ifdef _WIN32
    , bp::windows::create_no_window
#endif
  );
  err_reader = std::thread(std::bind(&Nvim::read_error_sync, this));
  out_reader = std::thread(std::bind(&Nvim::read_output_sync, this));
}

//static int file_num = 0;
//template<typename T>
//static void dump_to_file(const T& oh)
//{
  //std::string fname = std::to_string(file_num) + ".txt";
  //std::ofstream out {fname};
  //out << oh;
  //out.close();
  //++file_num;
//}

void Nvim::resize(const int new_width, const int new_height)
{
  send_notification("nvim_ui_try_resize", std::make_tuple(new_width, new_height));
}

static const std::unordered_map<std::string, bool> default_capabilities {
  {"ext_linegrid", true},
  //{"ext_multigrid", true},
  //{"ext_popupmenu", true},
  //{"ext_cmdline", true},
  {"ext_hlstate", true}
};

// Although this is synchronous, it will be performed on another thread.
void Nvim::read_output_sync()
{
  constexpr int buffer_maxsize = 1024 * 1024;
  auto buffer = std::make_unique<char[]>(buffer_maxsize);
  char* buf = buffer.get();
  while(!closed && running())
  {
    using std::size_t;
    auto msg_size = static_cast<size_t>(stdout_pipe.read(buf, buffer_maxsize));
    if (!msg_size) continue;
    std::string_view sv {buf, msg_size};
    std::size_t offset = 0;
    while(offset < msg_size)
    {
      auto parsed = Object::from_msgpack(sv, offset);
      auto* arr = parsed.array();
      if (!(arr && (arr->size() == 3 || arr->size() == 4))) continue;
      const auto msg_type = arr->at(0).u64();
      if (!msg_type) continue;
      switch(*msg_type)
      {
        case Type::Notification:
        {
          assert(arr->size() == 3);
          const auto method_str = arr->at(1).string();
          if (!method_str) continue;
          notification_handlers_mutex.lock();
          const auto func_it = notification_handlers.find(*method_str);
          if (func_it == notification_handlers.end())
          {
            notification_handlers_mutex.unlock();
          }
          else
          {
            const auto func = func_it->second;
            notification_handlers_mutex.unlock();
            func(std::move(parsed));
          }
          break;
        }
        case Type::Request:
        {
          assert(arr->size() == 4);
          const auto* method_str = arr->at(2).string();
          if (!method_str) continue;
          request_handlers_mutex.lock();
          const auto func_it = request_handlers.find(*method_str);
          if (func_it == request_handlers.end())
          {
            request_handlers_mutex.unlock();
          }
          else
          {
            const auto func = func_it->second;
            request_handlers_mutex.unlock();
            func(std::move(parsed));
          }
          break;
        }
        case Type::Response:
        {
          assert(arr->size() == 4);
          const auto msgid = arr->at(1).u64();
          assert(msgid);
          if (!msgid) continue;
          response_cb_mutex.lock();
          const auto cb_it = singleshot_callbacks.find(*msgid);
          if (cb_it != singleshot_callbacks.end())
          {
            const auto cb = cb_it->second;
            response_cb_mutex.unlock();
            cb(std::move(arr->at(3)), std::move(arr->at(2)));
          }
          else response_cb_mutex.unlock();
          break;
        }
        default:
          qWarning() << "Received an invalid msgpack message type: " << *msg_type << '\n';
          continue;
      }
    }
  }
  // Exiting. When Nvim closes both the error and output pipe close,
  // but we don't want to call the exit handler twice.
  // Make sure we're not adding an exit handler at the same time we're
  // calling it
  Lock exit_lock {exit_handler_mutex};
  on_exit_handler();
}

void Nvim::attach_ui(const int rows, const int cols)
{
  attach_ui(rows, cols, default_capabilities);
}

void Nvim::attach_ui(const int rows, const int cols, std::unordered_map<std::string, bool> capabilities)
{
  send_notification("nvim_ui_attach", std::make_tuple(rows, cols, std::move(capabilities)));
}
void Nvim::read_error_sync()
{
  // 500KB should be enough for stderr (not receving any huge input)
  constexpr int buffer_maxsize = 512 * 1024;
  auto buffer = std::make_unique<char[]>(buffer_maxsize);
  std::uint32_t bytes_read;
  bp::pipe& err_pipe = error.pipe();
  while(!closed && running())
  {
    bytes_read = err_pipe.read(buffer.get(), buffer_maxsize);
    if (bytes_read)
    {
      std::string s(buffer.get(), bytes_read);
      std::cout << "Error occurred: " << s << '\n';
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
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


bool Nvim::running()
{
  return nvim.running();
}

void Nvim::set_notification_handler(
  const std::string& method,
  msgpack_callback handler
)
{
  Lock lock {notification_handlers_mutex};
  notification_handlers.emplace(method, handler);
}

void Nvim::set_request_handler(
  const std::string& method,
  msgpack_callback handler
)
{
  Lock lock {request_handlers_mutex};
  request_handlers.emplace(method, handler);
}


void Nvim::command(const std::string& cmd)
{
  send_request("nvim_command", std::make_tuple(cmd));
}

void Nvim::send_input(
  const bool c,
  const bool s,
  const bool a,
  const bool d,
  const std::string& key,
  bool is_special
)
{
  std::string input_string;
  if (c || s || a || d || is_special)
  {
    using fmt::format;
    const std::string_view first = c ? "C-" : "";
    const std::string_view second = s ? "S-" : "";
    const std::string_view third = a ? "M-" : "";
    const std::string_view fourth = d ? "D-" : "";
    input_string = format("<{}{}{}{}{}>", first, second, third, fourth, key);
  }
  else
  {
    input_string = key;
  }
  send_input(input_string);
}

void Nvim::send_input(std::string key)
{
  send_notification("nvim_input", std::array<std::string, 1> {std::move(key)});
}

void Nvim::on_exit(std::function<void ()> handler)
{
  Lock lock {exit_handler_mutex};
  on_exit_handler = std::move(handler);
}

void Nvim::resize_cb(const int width, const int height, response_cb cb)
{
  send_request_cb("nvim_ui_try_resize", std::make_tuple(width, height), std::move(cb));
}

void Nvim::eval_cb(const std::string& expr, response_cb cb)
{
  send_request_cb("nvim_eval", std::make_tuple(expr), std::move(cb));
}

void Nvim::exec_viml(
  const std::string& str,
  bool capture_output,
  std::optional<response_cb> cb
)
{
  if (cb)
  {
    send_request_cb("nvim_exec", std::make_tuple(str, capture_output), *cb);
  }
  else
  {
    send_notification("nvim_exec", std::make_tuple(str, capture_output));
  }
}

void Nvim::input_mouse(
  std::string button,
  std::string action,
  std::string modifiers,
  int grid,
  int row,
  int col
)
{
  send_notification("nvim_input_mouse", std::tuple {
      std::move(button), std::move(action), std::move(modifiers),
      grid, row, col
  });
}


Nvim::~Nvim()
{
  // Close I/O Pipes and terminate process
  closed = true;
  nvim.terminate();
  error.pipe().close();
  stdout_pipe.close();
  stdin_pipe.close();
  out_reader.join();
  err_reader.join();
}
