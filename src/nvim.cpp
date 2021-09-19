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

template<typename T>
void Nvim::send_request(const std::string& method, const T& params, bool blocking)
{
  is_blocking.push_back(blocking);
  Lock lock {input_mutex};
  const std::uint64_t msg_type = Type::Request;
  msgpack::sbuffer sbuf;
  const auto msg = std::make_tuple(msg_type, current_msgid, method, params);
  msgpack::pack(sbuf, msg);
  // Potential for an exception when calling below code
  try
  {
    auto written = stdin_pipe.write(sbuf.data(), static_cast<int>(sbuf.size()));
    Q_UNUSED(written);
    assert(written);
    ++current_msgid;
  }
  catch (const std::exception& e)
  {
    std::cout << "Exception occurred: " << e.what() << '\n';
  }
}

template<typename T>
void Nvim::send_notification(const std::string& method, const T& params)
{
  // Same deal as Nvim::send_request, but for a notification this time
  Lock lock {input_mutex};
  const std::uint64_t msg_type = Type::Notification;
  msgpack::sbuffer sbuf;
  const auto msg = std::make_tuple(msg_type, method, params);
  msgpack::pack(sbuf, msg);
  try
  {
    auto written = stdin_pipe.write(sbuf.data(), static_cast<int>(sbuf.size()));
    Q_UNUSED(written);
    assert(written);
  }
  catch (const std::exception& e)
  {
    std::cout << "Exception occurred: " << e.what() << '\n';
  }
}

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
    //using Clock = std::chrono::high_resolution_clock;
    //using dur = std::chrono::duration<double, std::milli>;
    while(offset < msg_size)
    {
      auto parsed = Object::from_msgpack(sv, offset);
      //fmt::print("{}\n", parsed.to_string());
      auto* arr = parsed.array();
      if (!(arr && (arr->size() == 3 || arr->size() == 4))) continue;
      const auto msg_type = arr->at(0).u64();
      if (!msg_type) continue;
      switch(*msg_type)
      {
        case Type::Notification:
        {
          assert(arr->size() == 3);
          const auto method_qstr = arr->at(1).string();
          if (!method_qstr) continue;
          auto method = method_qstr->toStdString();
          notification_handlers_mutex.lock();
          const auto func_it = notification_handlers.find(method);
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
          const QString* method_qstr = arr->at(2).string();
          if (!method_qstr) continue;
          const auto method = method_qstr->toStdString();
          request_handlers_mutex.lock();
          const auto func_it = request_handlers.find(method);
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
          assert(*msgid < is_blocking.size());
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
  std::cout << "Output closed." << std::endl;
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

template<typename T>
void Nvim::send_request_cb(
  const std::string& method,
  const T& params,
  response_cb cb
)
{
  std::unique_lock<std::mutex> lock {response_cb_mutex};
  singleshot_callbacks[current_msgid] = std::move(cb);
  send_request(method, params, false);
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


void Nvim::send_response(
  std::uint64_t msgid,
  msgpack::object res,
  msgpack::object err
)
{
  Lock lock {input_mutex};
  const std::uint64_t type = Type::Response;
  auto&& msg = std::tuple {type, msgid, err, res};
  msgpack::sbuffer sbuf;
  msgpack::pack(sbuf, msg);
  try
  {
    stdin_pipe.write(sbuf.data(), static_cast<int>(sbuf.size()));
  }
  catch(...)
  {
    fmt::print("Could not send response. Msgid: {}\n", msgid);
  }
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
