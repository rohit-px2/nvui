#ifndef NVUI_NVIM_HPP
#define NVUI_NVIM_HPP

#include <boost/process/pipe.hpp>
#include <boost/process.hpp>
#include <unordered_map>
#include <vector>
#include <functional>
#include <thread>
#include <msgpack.hpp>
#include <atomic>
#include <optional>

enum Type : std::uint64_t {
  Request = 0,
  Response = 1,
  Notification = 2
};
enum Notifications : std::uint8_t;
enum Request : std::uint8_t;
using msgpack_callback = std::function<void (msgpack::object_handle*)>;
/// The Nvim class contains an embedded Neovim instance and
/// some useful functions to receive output and send input
/// using the msgpack-rpc protocol.
class Nvim
{
private:
  using response_cb = std::function<void (msgpack::object, msgpack::object)>;
public:
  ~Nvim();
  /**
   * Constructs an embedded Neovim instance and establishes communication.
   * The Neovim instance is created with the command "nvim --embed".
   */
  Nvim(std::string path = "", std::vector<std::string> args = {"--embed"});
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
   * Send an "nvim_ui_try_resize" message to Neovim, indicating to Neovim to resize
   * its area (in terms of rows and columns of text).
   * You should have called attach_ui before calling this.
   */
  void resize(const int new_width, const int new_height);
  /**
   * Sends keyboard input to Nvim with the corresponding modifiers.
   * If the key is a "Special" key, then it's given the angle brackets, even if
   * there are no modifiers attached. This is needed for things like <LT>, <Space>, <Tab> etc.
   */
  void send_input(const bool ctrl, const bool shift, const bool alt, const std::string& key, bool is_special = false);
  /**
   * Sends the nvim_input message directly to Nvim with no processing,
   * which means you should have done the processing on your end.
   * If you haven't done any processing, then call send_input(shift, ctrl, alt, key)
   * which does the processing automatically.
   */
  void send_input(std::string key);
  /**
   * Sends a "nvim_ui_attach" message to the embedded Neovim instance with
   * the rows and cols as the parameters, along with the default capabilities.
   */
  void attach_ui(const int rows, const int cols);
  /**
   * Sends an "nvim_ui_attach" message to Neovim with the given rows, columns,
   * and client capabilities.
   */
  void attach_ui(const int rows, const int cols, std::unordered_map<std::string, bool> capabilities);
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
  /**
   * Sets the notification handler for the given method.
   * When a notification is sent, and the method matches the name
   * of the method passed, the corresponding function will be called
   * with the parameters of the message.
   * NOTE: For redraw events, you should call this before
   * calling attach_ui (so that you don't miss redraw events).
   * Another NOTE: The handler will be called on a separate thread,
   * so make sure it's thread-safe.
   */
  void set_notification_handler(
    const std::string& method,
    msgpack_callback handler
  );
  /**
   * Sets a request handler for the given method.
   * Same as set_notification_handler, but for requests.
   */
  void set_request_handler(
    const std::string& method,
    msgpack_callback handler
  );
  /**
   * Runs cmd in Neovim.
   * This can be used to set autocommands, among other things.
   */
  void command(const std::string& cmd);
  /**
   * Returns the api info of the attached Neovim instance.
   */
  msgpack::object_handle get_api_info();
  /**
   * Attach a function which is called when Neovim exits.
   */
  void on_exit(std::function<void ()> handler);
  /**
   * Send a request and execute a callback with the response and error
   * objects when the response is received.
   * NOTE: For callbacks, cb is run on the Neovim thread.
   * Make sure you take precautions before handling the data on another
   * thread.
   */
  template<typename T>
  void send_request_cb(
    const std::string& method,
    const T& params,
    response_cb cb
  );
  /**
   * Resize and send a callback when a response is received.
   */
  void resize_cb(const int width, const int height, response_cb cb);
  /**
   * Evaluate the VimL expression and call the given callback with
   * the response/error.
   */
  void eval_cb(const std::string& expr, response_cb cb);
  /**
   * Execute a block of VimL code. If response_cb contains a callback,
   * the callback is called with the result.
   * If capture_output is true, any output (e.g. printing) in the
   * code becomes part of the result.
   */
  void exec_viml(
    const std::string& str,
    bool capture_output = false,
    std::optional<response_cb> cb = std::nullopt
  );
  /**
   * Send a mouse input event with the given parameters.
   * Corresponds directly to Neovim API's nvim_input_mouse function.
   */
  void input_mouse(
    std::string button,
    std::string action,
    std::string modifiers,
    int grid,
    int row,
    int col
  );
private:
  std::function<void ()> on_exit_handler = [](){};
  std::unordered_map<std::string, msgpack_callback> notification_handlers;
  std::unordered_map<std::string, msgpack_callback> request_handlers;
  std::unordered_map<std::uint32_t, response_cb> singleshot_callbacks;
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
  std::mutex notification_handlers_mutex;
  std::mutex request_handlers_mutex;
  std::mutex exit_handler_mutex;
  std::mutex response_cb_mutex;
  std::uint32_t num_responses;
  std::uint32_t current_msgid;
  boost::process::group proc_group;
  boost::process::child nvim;
  boost::process::pipe stdout_pipe;
  boost::process::pipe stdin_pipe;
  boost::process::ipstream error;
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
