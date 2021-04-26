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
  std::uint64_t current_msgid;
  std::vector<char> read_buffer;
  boost::process::child nvim;
  boost::asio::io_service ios;
  boost::asio::mutable_buffer mut_buf;
  boost::process::async_pipe read;
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
  void send_request(const std::string& method, const T& params);
  template<typename T>
  void send_notification(const std::string& method, const T& params);
  void read_output(boost::process::async_pipe& p, boost::asio::mutable_buffer& buf);
public:
  std::thread reader;
  ~Nvim();
  Nvim();
  bool nvim_running();
  void nvim_resize(const int new_rows, const int new_cols);
  void nvim_send_input(const bool shift, const bool ctrl, const std::uint16_t key);
  void attach_ui(const int rows, const int cols);
};
