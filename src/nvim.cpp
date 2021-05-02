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
using std::this_thread::sleep_for;

static const std::vector<int> EMPTY_LIST {};
enum Notifications : std::uint8_t {
  nvim_ui_attach = 0,
  nvim_try_resize = 1,
  nvim_set_var = 2
};

enum Request : std::uint8_t {
  nvim_get_api_info = 0,
  nvim_input = 1,
  nvim_input_mouse = 2,
  nvim_eval = 3,
  nvim_command = 4 
};

constexpr auto one_ms = std::chrono::milliseconds(1);

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

// ###################### DONE SETTING UP ##################################

static inline std::uint32_t to_uint(char ch)
{
  return static_cast<std::uint32_t>(static_cast<unsigned char>(ch));
}

/// Constructor
Nvim::Nvim()
: error(),
  proc_group(),
  stdin_pipe(),
  stdout_pipe(),
  current_msgid(0),
  num_responses(0),
  closed(false)
{
  // Everything is going to get detached because we're keeping the process running,
  // the message processing thread running so that it doesn't get destroyed.
  err_reader = std::thread(std::bind(&Nvim::read_error_sync, this));
  err_reader.detach();
  out_reader = std::thread(std::bind(&Nvim::read_output_sync, this));
  out_reader.detach();
  auto nvim_path = bp::search_path("nvim");
  nvim = bp::child(
    nvim_path,
    "--embed",
    bp::std_out > stdout_pipe,
    bp::std_in < stdin_pipe,
    proc_group
  );
  nvim.detach();
  proc_group.detach();
}

static int file_num = 0;
static void dump_to_file(const msgpack::object_handle& oh)
{
  std::string fname = std::to_string(file_num) + ".json"; // Should be valid json once decoded
  std::ofstream out {fname};
  out << oh.get();
  out.close();
  ++file_num;
}

template<typename T>
void Nvim::send_request(const std::string& method, const T& params, bool blocking)
{
  is_blocking.push_back(blocking);
  Lock lock {input_mutex};
  const std::uint64_t msg_type = Type::Request;
  msgpack::sbuffer sbuf;
  const auto msg = std::make_tuple(msg_type, current_msgid, method, params);
  msgpack::pack(sbuf, msg);
  int written = stdin_pipe.write(sbuf.data(), sbuf.size());
  ++current_msgid;
  assert(written);
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
  int written = stdin_pipe.write(sbuf.data(), sbuf.size());
  assert(written);
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
  msgpack::object_handle oh;
  msgpack::object obj;
  //std::tuple<int, std::string, 
  std::cout << std::dec;
  // buffer_maxsize of 1MB
  constexpr int buffer_maxsize = 1024 * 1024;
#ifdef _WIN32
  // On Windows we can use Readfile to get the underlying data, working with ipstream
  // has not been going well.
  std::unique_ptr<char[]> buffer(new char[buffer_maxsize]);
  DWORD msg_size;
  Handle output_read = stdout_pipe.native_source();
  while(!closed)
  {
    bool read_success = ReadFile(
      output_read, buffer.get(), buffer_maxsize, &msg_size, nullptr
    );
    if (read_success)
    {
      using std::cout;
      oh = msgpack::unpack(buffer.get(), msg_size);
      obj = oh.get();
      // According to msgpack-rpc spec, this must be an array
      assert(obj.type == msgpack::type::ARRAY);
      msgpack::object_array arr = obj.via.array;
      // Size of the array is either 3 (Notificaion) or 4 (Request / Response)
      assert(arr.size == 3 || arr.size == 4);
      // Otherwise, we have a request/response, both of which have the same signature.
      const std::uint64_t type = arr.ptr[0].as<std::uint64_t>();
      //cout << "Msg type: " << type << "\n";
      //cout << "Object: " << obj << std::endl;
      // The type should only ever be one of Request, Notification, or Response.
      switch(type)
      {
        case Type::Request:
        {
          cout << "Got a request!\n";
          break;
        }
        case Type::Notification:
        {
          cout << "Got a notification!\n";
          const std::string method = arr.ptr[1].as<std::string>();
          if (method == "redraw")
          {
            std::cout << "Time to handle redraw.\n";
          }
          break;
        }
        case Type::Response:
        {
          const std::uint64_t msgid = arr.ptr[1].as<std::uint64_t>();
          std::cout << "Message id: " << msgid << '\n';
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
              std::cout << "There was an error\n";
              last_response = arr.ptr[2];
            }
            else
            {
              std::cout << "No error!\n";
              last_response = arr.ptr[3];
            }
          }
          break;
        }
        default:
        {
          throw std::exception("Message was not a valid msgpack-rpc message.");
          return;
        }
      }
      dump_to_file(oh);
    }
    else
    {
      // No bytes were read / ReadFile failed, wait for a bit before trying again.
      // (Should also help prevent super high CPU usage when idle)
      //sleep_for(std::chrono::microseconds(100));
    }
  }
#endif
  std::cout << "Output closed." << std::endl;
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
  constexpr int timeout = 100;
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
  return msgpack::object_handle();
}

msgpack::object_handle Nvim::eval(const std::string& expr)
{
  return send_request_sync("nvim_eval", std::make_tuple(expr));
}
void Nvim::read_error_sync()
{
  std::string line;
  while(std::getline(error, line) && !closed)
  {
    std::cout << "ERROR:\n" << line << "\n" << std::endl;
    // Poll every 0.01s
    sleep_for(std::chrono::milliseconds(10));
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

Nvim::~Nvim()
{
  // Close I/O Pipes and terminate process
  closed = true;
  nvim.terminate();
  error.pipe().close();
  stdout_pipe.close();
  stdin_pipe.close();
  if (out_reader.joinable())
  {
    out_reader.join();
  }
  if (err_reader.joinable())
  {
    err_reader.join();
  }
}
