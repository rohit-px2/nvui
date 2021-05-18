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
  err_reader = std::thread(std::bind(&Nvim::read_error_sync, this));
  out_reader = std::thread(std::bind(&Nvim::read_output_sync, this));
  const auto nvim_path = bp::search_path("nvim");
  if (nvim_path.empty())
  {
    throw std::exception("Neovim not found in PATH");
  }
  nvim = bp::child(
    nvim_path,
    "--embed",
    bp::std_out > stdout_pipe,
    bp::std_in < stdin_pipe,
    bp::std_err > error,
    proc_group
  );
  nvim.detach();
  proc_group.detach();
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
    int written = stdin_pipe.write(sbuf.data(), sbuf.size());
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
    int written = stdin_pipe.write(sbuf.data(), sbuf.size());
    assert(written);
  }
  catch (const std::exception& e)
  {
    std::cout << "Exception occurred: " << e.what() << '\n';
  }
}

void Nvim::resize(const int new_rows, const int new_cols)
{
  send_notification("nvim_ui_try_resize", std::make_tuple(new_rows, new_cols));
}

static const std::unordered_map<std::string, bool> default_capabilities {
  {"ext_linegrid", true},
  {"ext_popupmenu", true},
  {"ext_cmdline", true},
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
  std::unique_ptr<char[]> buffer(new char[buffer_maxsize]);
  auto buf = buffer.get();
  std::int64_t msg_size;
  while(!closed)
  {
    msg_size = stdout_pipe.read(buf, buffer_maxsize);
    cout << "Message size: " << msg_size << '\n';
    if (msg_size)
    {
      oh = msgpack::unpack(buf, msg_size);
      const msgpack::object& obj = oh.get();
      // According to msgpack-rpc spec, this must be an array
      assert(obj.type == msgpack::type::ARRAY);
      const msgpack::object_array arr = obj.via.array;
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
            func_it->second(arr.ptr[3]);
          }
          break;
        }
        case Type::Notification:
        {
          assert(arr.size == 3);
          cout << "Got a notification!\n";
          const std::string method = arr.ptr[1].as<std::string>();
          // Lock while reading
          Lock read_lock {notification_handlers_mutex};
          const auto func_it = notification_handlers.find(method);
          if (func_it != notification_handlers.end())
          {
            // Call handler on the 3rd object (params)
            func_it->second(arr.ptr[2]);
          }
          break;
        }
        case Type::Response:
        {
          assert(arr.size == 4);
          const std::uint32_t msgid = arr.ptr[1].as<std::uint32_t>();
          cout << "Message id: " << msgid << '\n';
          assert(0 <= msgid && msgid < is_blocking.size());
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
          break;
        }
        default:
        {
          // Should never happen
          assert(!"Message was not a valid msgpack-rpc message");
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
  cout << "Output closed." << std::endl;
}

void Nvim::attach_ui(const int rows, const int cols)
{
  std::cout << "Attaching UI. Please wait..." << std::endl;
  const auto params = std::make_tuple(rows, cols, default_capabilities);
  send_notification("nvim_ui_attach", params);
  std::cout << "All Done!" << std::endl;
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
  throw std::exception("Message not received");
}

msgpack::object_handle Nvim::eval(const std::string& expr)
{
  return send_request_sync("nvim_eval", std::make_tuple(expr));
}

void Nvim::read_error_sync()
{
  // 500KB should be enough for stderr (not receving any huge input)
  constexpr int buffer_maxsize = 512 * 1024;
  std::unique_ptr<char[]> buffer(new char[buffer_maxsize]);
  std::uint32_t bytes_read;
  bp::pipe& err_pipe = error.pipe();
  while(!closed)
  {
    bytes_read = err_pipe.read(buffer.get(), buffer_maxsize);
    if (bytes_read)
    {
      std::string s(buffer.get(), bytes_read);
      std::cout << "Error occurred: " << s << '\n';
    }
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
