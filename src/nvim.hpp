#pragma once
#include <boost/asio.hpp>
#include <boost/process/pipe.hpp>
#include <msgpack.hpp>
#include <boost/process.hpp>
#include <windows.h>
#include <memory.h>

/// The Nvim class is responsible for communicating with the embedded Neovim instance
/// through the msgpack-rpc API.
/// All communication between the GUI and Neovim is to use the Nvim class.
class Nvim
{
private:
  boost::asio::io_service ios;
  boost::process::async_pipe ap;
  boost::process::child nvim;
  boost::process::opstream in;
  boost::asio::streambuf buf;
  //PROCESS_INFORMATION process_info;
  //HANDLE write_stdin;
  //HANDLE read_stdout;
  //HANDLE read_stdin;
  //HANDLE write_stdout;
  void read_output(const boost::system::error_code &ec, std::size_t size);
  void decide(const std::string& msg);
public:
  Nvim();
  void send_input(std::uint8_t key);
};
