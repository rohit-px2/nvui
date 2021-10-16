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
#include "log.hpp"

#ifdef _WIN32
#include <boost/process/windows.hpp>
#endif

namespace bp = boost::process;
using Lock = std::lock_guard<std::mutex>;

/// Constructor
Nvim::Nvim()
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
}

void Nvim::open_local(std::string path, std::vector<std::string> args)
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
  LOG_INFO("Using nvim executable at: {}", nvim_path.string());
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
  err_reader = std::thread([&] { read_error_sync(); });
  out_reader = std::thread([&] { read_output_sync(); });
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
  LOG_TRACE("Resizing Neovim to width {} and height {}", new_width, new_height);
  send_notification("nvim_ui_try_resize", std::make_tuple(new_width, new_height));
}

static const std::unordered_map<std::string, bool> default_capabilities {
  {"ext_linegrid", true},
  //{"ext_multigrid", true},
  //{"ext_popupmenu", true},
  //{"ext_cmdline", true},
  {"ext_hlstate", true}
};

/// Although this is synchronous, it will be performed on another thread.
void Nvim::read_output_sync()
{
  using std::cout;
  cout << std::dec;
  // buffer_maxsize of 1MB
  constexpr int buffer_maxsize = 1024 * 1024;
  auto buffer = std::make_unique<char[]>(buffer_maxsize);
  char* buf = buffer.get();
  while(!closed && running())
  {
    int msg_size = stdout_pipe.read(buf, buffer_maxsize);
    msgpack::unpacker unpacker;
    unpacker.reserve_buffer(msg_size);
    memcpy(unpacker.buffer(), buf, msg_size);
    unpacker.buffer_consumed(msg_size);
    // There can be multiple messages inside of the buffer
    //std::size_t offset = 0;
    msgpack::object_handle oh;
    while(unpacker.next(oh))
    {
      //oh = msgpack::unpack(buf, msg_size, offset);
      const msgpack::object& obj = oh.get();
      log::lazy_trace([&] {
        std::stringstream ss;
        ss << "Received messagepack object: " << obj;
        return ss.str();
      });
      // According to msgpack-rpc spec, this must be an array
      if (obj.type != msgpack::type::ARRAY) continue;
      const msgpack::object_array& arr = obj.via.array;
      // Size of the array is either 3 (Notificaion) or 4 (Request / Response)
      if (!(arr.size == 3 || arr.size == 4)) continue;
      if (!(arr.ptr[0].type == msgpack::type::POSITIVE_INTEGER)) continue;
      const std::uint32_t type = arr.ptr[0].as<std::uint32_t>();
      // The type should only ever be one of Request, Notification, or Response.
      if (!(type == Type::Notification
          || type == Type::Response
          || type == Type::Request)) continue;
      switch(type)
      {
        case Type::Request:
        {
          if (!(arr.size == 4)) continue;
          if (!(arr.ptr[2].type == msgpack::type::STR)) continue;
          const std::string method = arr.ptr[2].as<std::string>();
          // Lock while reading
          Lock read_lock {request_handlers_mutex};
          const auto func_it = request_handlers.find(method);
          if (func_it != request_handlers.end())
          {
            // Params is the last element in the 4-long array
            func_it->second(&oh);
          }
          break;
        }
        case Type::Notification:
        {
          if (!(arr.size == 3)) continue;
          if (!(arr.ptr[1].type == msgpack::type::STR)) continue;
          const std::string method = arr.ptr[1].as<std::string>();
          // Lock while reading
          Lock read_lock {notification_handlers_mutex};
          const auto func_it = notification_handlers.find(method);
          if (func_it != notification_handlers.end())
          {
            // Call handler on the 3rd object (params)
            func_it->second(&oh);
          }
          break;
        }
        case Type::Response:
        {
          if (!(arr.size == 4)) continue;
          if (!(arr.ptr[0].type == msgpack::type::POSITIVE_INTEGER)) continue;
          const std::uint32_t msgid = arr.ptr[1].as<std::uint32_t>();
          //cout << "Message id: " << msgid << '\n';
          if (!(msgid < is_blocking.size())) continue;
          // If it's a blocking request, the other thread is waiting for
          // response_received
          if (is_blocking[msgid])
          {
            // We'll lock just to be safe.
            // I think if we set response_received = true after
            // setting the data, it might allow for thread-safe behaviour
            // without locking, but we'll t(h)read on the safe side for now
            Lock lock {response_mutex};
            // Check if we got an error
            response_received = true;
            if (!arr.ptr[2].is_nil())
            {
              cout << "There was an error\n";
              last_response = arr.ptr[2];
            }
            else
            {
              cout << "No error!\n";
              last_response = arr.ptr[3];
            }
          }
          else
          {
            Lock lock {response_cb_mutex};
            LOG_TRACE("Received response for message id {}", msgid);
            if (singleshot_callbacks.contains(msgid))
            {
              const auto& cb = singleshot_callbacks.at(msgid);
              if (!arr.ptr[2].is_nil())
              {
                cb(msgpack::object(), arr.ptr[2]);
              }
              else
              {
                cb(arr.ptr[3], msgpack::object());
              }
              singleshot_callbacks.erase(msgid);
            }
          }
          break;
        }
        default: continue;
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

template<typename T>
void Nvim::set_var(const std::string& name, const T& val)
{
  send_notification("nvim_set_var", std::make_tuple(name, val));
}

template void Nvim::set_var<int>(const std::string&, const int&);
template void Nvim::set_var<std::string>(const std::string&, const std::string&);

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
  LOG_INFO("Keyboard input: {}", key);
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
  LOG_INFO(
    "Mouse input: Button: {}, Action: {}, Modifiers: {}, Grid: {}, Row: {}, Col: {}",
    button, action, modifiers, grid, row, col
  );
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
