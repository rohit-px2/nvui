#pragma once
#include <boost/asio.hpp>
#include <boost/process/pipe.hpp>
#include <boost/process.hpp>
#include <windows.h>
#include <memory.h>
#include <iostream>
#include <boost/bind.hpp>
#include <boost/thread.hpp>
#include <optional>
using Handle = HANDLE;
using StartupInfo = STARTUPINFO;
using SecAttribs = SECURITY_ATTRIBUTES;
using DWord = DWORD;
enum Type : std::uint8_t {
  Request = 0,
  Response = 1,
  Notification = 2
};
static constexpr const int MAX_MSG_SIZE = 4096;
enum Notifications : std::uint8_t;
enum Request : std::uint8_t;
/// The Nvim class is responsible for communicating with the embedded Neovim instance
/// through the msgpack-rpc API.
/// All communication between the GUI and Neovim is to use the Nvim class.
class Nvim
{
  std::uint32_t num_responses;
  std::uint32_t current_msgid;
  boost::process::group proc_group;
  boost::process::child nvim;
  boost::process::pipe stdout_pipe;
  boost::process::pipe stdin_pipe;
  boost::process::ipstream error;
  boost::process::ipstream output;
  boost::process::opstream write;
  struct {
    Handle stdin_write;
    Handle stdout_write;
    Handle stdin_read;
    Handle stdout_read;
  } handles;
  void decide(const std::string& msg);
  template<typename T>
  void send_request(const std::string& method, const T& params, int size = 1);
  template<typename T>
  void send_notification(const std::string& method, const T& params);
  void read_output(boost::process::async_pipe& p, boost::asio::mutable_buffer& buf);
  void read_output_sync();
  void read_error_sync();
public:
  std::thread err_reader;
  std::thread out_reader;
  ~Nvim();
  Nvim();
  int exit_code();
  bool nvim_running();
  void nvim_resize(const int new_rows, const int new_cols);
  void nvim_send_input(const bool shift, const bool ctrl, const std::uint16_t key);
  void attach_ui(const int rows, const int cols);
};

// This is needed for msgpack (Neovim expects byte-strings, which are non-UTF8,
// while msgpack automatically packs strings into UTF-8 strings).
// Converts the string str into a vector of characters.
std::vector<char> stov(const std::string& str);
