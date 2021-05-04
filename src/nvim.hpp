#ifndef NVUI_NVIM_HPP
#define NVUI_NVIM_HPP

#include <boost/process/pipe.hpp>
#include <boost/process.hpp>
#include <vector>
#include <windows.h>
#include <thread>
#include <msgpack.hpp>
#include <atomic>
using Handle = HANDLE;
using StartupInfo = STARTUPINFO;
using SecAttribs = SECURITY_ATTRIBUTES;
enum Type : std::uint64_t {
  Request = 0,
  Response = 1,
  Notification = 2
};
enum Notifications : std::uint8_t;
enum Request : std::uint8_t;

/// The Nvim class contains an embedded Neovim instance and
/// some useful functions to receive output and send input
/// using the msgpack-rpc protocol.
class Nvim
{
public:
  ~Nvim();
  /**
   * Constructs an embedded Neovim instance and establishes communication.
   * The Neovim instance is created with the command "nvim --embed".
   */
  Nvim();
  /**
   * Get the exit code of the Neovim instance.
   * If Neovim is still running, the exit code that is return will be INT_MIN.
   */
  int exit_code();
  /**
   * Returns true if the embedded Neovim instance is still running (false otherwise).
   */
  bool running();
  /**
   * TODO: Add documentation
   */
  void resize(const int new_rows, const int new_cols);
  /**
   * TODO: Add documentation
   */
  void send_input(const bool shift, const bool ctrl, const std::uint16_t key);
  /**
   * Sends a "nvim_ui_attach" message to the embedded Neovim instance with
   * the rows and cols as the parameters, along with the default capabilities.
   */
  void attach_ui(const int rows, const int cols);
  /**
   * Evaluates expr as a VimL expression and returns the result as a msgpack
   * object handle. The underlying msgpack object can be obtained using
   * msgpack::object_handle::get(), and can then be converted into the
   * desired type using msgpack::object::as<T>() (if it is possible to convert
   * it, otherwise it will throw an exception).
   * NOTE: You should either immediately convert the result to a different type,
   * or keep it as msgpack::object_handle. If you call get() immediately,
   * the msgpack::object_handle will have no references and will thus destroy
   * the object data.
   */
  msgpack::object_handle eval(const std::string& expr);
  /**
   * Sends an "nvim_set_var" message, setting a global variable (g:var)
   * with the value val.
   * Note: Only defined for values of type std::string and int.
   */
  template<typename T>
  void set_var(const std::string& name, const T& val);
private:
  std::thread err_reader;
  std::thread out_reader;
  // Condition variable to check if we are closing
  std::atomic<bool> closed;
  // This and last_response, along with response_mutex, are meant for
  // performing a blocking request for the data of the response.
  std::vector<std::uint8_t> is_blocking;
  std::atomic<bool> response_received;
  msgpack::object last_response;
  std::mutex response_mutex;
  std::mutex input_mutex;
  std::uint32_t num_responses;
  std::uint32_t current_msgid;
  boost::process::group proc_group;
  boost::process::child nvim;
  boost::process::pipe stdout_pipe;
  boost::process::pipe stdin_pipe;
  boost::process::ipstream error;
  void decide(const std::string& msg);
  template<typename T>
  void send_request(const std::string& method, const T& params, bool blocking = false);
  template<typename T>
  void send_notification(const std::string& method, const T& params);
  void read_output_sync();
  void read_error_sync();
  template<typename T>
  msgpack::object_handle send_request_sync(const std::string& method, const T& params);
};

#endif
