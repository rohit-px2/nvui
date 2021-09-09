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

#ifdef _WIN32
#include <boost/process/windows.hpp>
#endif

namespace bp = boost::process;
namespace ba = boost::asio;
namespace baip = boost::asio::ip;

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
Nvim::Nvim()
: notification_handlers(),
  request_handlers(),
  closed(false),
  num_responses(0),
  current_msgid(0),
  proc_group(),
  stdout_pipe(),
  stdin_pipe(),
  error(),
  ios(),
  socket(ios)
{
}

void Nvim::open_local(
  const std::string& path,
  const std::vector<std::string>& args
)
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
  connection_mode = ConnectionMode::Local;
}

void Nvim::open_tcp(const std::string& addr_and_port)
{
  const auto colon_pos = addr_and_port.find(":");
  if (colon_pos == std::string::npos)
  {
    throw std::runtime_error("Invalid format of TCP address");
  }
  const auto host = addr_and_port.substr(0, colon_pos);
  const auto port_str = addr_and_port.substr(colon_pos + 1);
  using baip::tcp;
  tcp::resolver resolver(ios);
  tcp::resolver::query q(host, port_str, tcp::resolver::numeric_service);
  auto endpoints = resolver.resolve(q);
  if (endpoints.size() == 0)
  {
    throw std::runtime_error("No endpoints with this name.");
  }
  baip::tcp::endpoint endpoint = endpoints->endpoint();
  socket.connect(endpoint);
  connection_mode = ConnectionMode::Tcp;
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

/// Although this is synchronous, it will be performed on another thread.
void Nvim::read_output_sync()
{
  using std::cout;
  msgpack::object_handle oh;
  cout << std::dec;
  // buffer_maxsize of 1MB
  constexpr int buffer_maxsize = 1024 * 1024;
  auto buffer = std::make_unique<char[]>(buffer_maxsize);
  char* buf = buffer.get();
  while(!is_closed())
  {
    int msg_size = read(buf, buffer_maxsize);
    if (msg_size > buffer_maxsize)
    {
      using fmt::format;
      throw std::runtime_error(format(
        "Message of size {} could not fit in buffer of size {}\n",
        msg_size, buffer_maxsize
      ));
    }
    if (msg_size > 0)
    {
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
        // According to msgpack-rpc spec, this must be an array
        assert(obj.type == msgpack::type::ARRAY);
        const msgpack::object_array& arr = obj.via.array;
        // Size of the array is either 3 (Notificaion) or 4 (Request / Response)
        assert(arr.size == 3 || arr.size == 4);
        const std::uint32_t type = arr.ptr[0].as<std::uint32_t>();
        // The type should only ever be one of Request, Notification, or Response.
        assert(type == Type::Notification || type == Type::Response || type == Type::Request);
        switch(type)
        {
          case Type::Request:
          {
            assert(arr.size == 4);
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
            assert(arr.size == 3);
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
            assert(arr.size == 4);
            const std::uint32_t msgid = arr.ptr[1].as<std::uint32_t>();
            //cout << "Message id: " << msgid << '\n';
            assert(msgid < is_blocking.size());
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
          default:
          {
            // Should never happen
            assert(!"Message was not a valid msgpack-rpc message");
            fmt::print("Message was not a valid msgpack-rpc message\n");
          }
        }
      }
      //dump_to_file(oh.get());
    }
    else
    {
      // No bytes were read / ReadFile failed, wait for a bit before trying again.
      // (Should also help prevent super high CPU usage when idle)
      //sleep_for(std::chrono::microseconds(100));
    }
  }
  // Exiting. When Nvim closes both the error and output pipe close,
  // but we don't want to call the exit handler twice.
  // Make sure we're not adding an exit handler at the same time we're
  // calling it
  Lock exit_lock {exit_handler_mutex};
  on_exit_handler();
  cout << "Output closed." << std::endl;
}

void Nvim::attach_ui(const int rows, const int cols)
{
  attach_ui(rows, cols, default_capabilities);
}

void Nvim::attach_ui(const int rows, const int cols, std::unordered_map<std::string, bool> capabilities)
{
  send_notification("nvim_ui_attach", std::make_tuple(rows, cols, std::move(capabilities)));
}

template<typename T>
msgpack::object_handle Nvim::send_request_sync(const std::string& method, const T& params)
{
  int count = 0;
  send_request(method, params, true);
  while(true)
  {
    ++count;
    // Locking and unlocking is expensive, checking an atomic is relatively cheaper.
    if (response_received)
    {
      // To prevent references becoming invalid, copy the data of the object to another
      // place.
      msgpack::object_handle new_obj;
      Lock resp_lock {response_mutex};
      // Copy last_response into new_obj
      new_obj = msgpack::clone(last_response);
      response_received = false;
      return new_obj;
    }
  }
  std::cout << "Didnt get the result\n";
  // This shouldn't ever activate since we'll just block forever
  // if we don't receive
  throw std::runtime_error("Message not received");
}

msgpack::object_handle Nvim::eval(const std::string& expr)
{
  return send_request_sync("nvim_eval", std::make_tuple(expr));
}

void Nvim::read_error_sync()
{
  // 500KB should be enough for stderr (not receving any huge input)
  constexpr int buffer_maxsize = 512 * 1024;
  auto buffer = std::make_unique<char[]>(buffer_maxsize);
  bp::pipe& err_pipe = error.pipe();
  while(!is_closed())
  {
    int bytes_read = err_pipe.read(buffer.get(), buffer_maxsize);
    if (bytes_read)
    {
      std::string s(buffer.get(), bytes_read);
      std::cout << "Error occurred: " << s << '\n';
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  std::cout << "Error closed\n";
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
  switch(connection_mode)
  {
    case ConnectionMode::Local:
      return nvim.running();
    case ConnectionMode::Tcp:
      return socket.is_open();
  }
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

msgpack::object_handle Nvim::get_api_info()
{
  return send_request_sync("nvim_get_api_info", std::array<int, 0> {});
}

void Nvim::send_input(const bool ctrl, const bool shift, const bool alt, const std::string& key, bool is_special)
{
  std::string input_string;
  if (ctrl || shift || alt || is_special)
  {
    const std::string first = ctrl ? "C-" : "";
    const std::string second = shift ? "S-" : "";
    const std::string third = alt ? "M-" : "";
    input_string = fmt::format("<{}{}{}{}>", first, second, third, key);
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

void Nvim::write(const char* str, std::size_t size)
{
  switch(connection_mode)
  {
    case ConnectionMode::Local:
      stdin_pipe.write(str, static_cast<int>(size));
      break;
    case ConnectionMode::Tcp:
      socket.write_some(ba::buffer(str, size));
      break;
  }
}

int Nvim::read(char* str, std::size_t max_size)
{
  switch(connection_mode)
  {
    case ConnectionMode::Local:
      return stdout_pipe.read(str, static_cast<int>(max_size));
      break;
    case ConnectionMode::Tcp:
      return static_cast<int>(socket.read_some(ba::buffer(str, max_size)));
      break;
  }
}

void Nvim::set_client_info(
  ClientInfo client_info
)
{
  send_notification("nvim_set_client_info", client_info);
}

Nvim::~Nvim()
{
  // Close I/O Pipes and terminate process
  closed = true;
  socket.close();
  ios.stop();
  nvim.terminate();
  error.pipe().close();
  stdout_pipe.close();
  stdin_pipe.close();
  out_reader.join();
  err_reader.join();
}
