#include "nvim.hpp"
#include "utils.hpp"
#include <catch2/catch.hpp>
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

TEST_CASE("nvim_set_var sets variables properly", "[nvim_set_var]")
{
  using std::this_thread::sleep_for;
  using namespace std::chrono_literals;
  using obj = msgpack::object;
  // One thing to note: nvim_set_var only works for setting strings and ints
  // (as is said in the header file). However it's just a template method
  // so adding more types is as easy as adding more declarations in the cpp file.
  Nvim nvim;
  nvim.open_local();
  REQUIRE(nvim.running());
  SECTION("nvim_set_var works for ints")
  {
    std::atomic<bool> done = false;
    nvim.set_var("uniquevariable", 253);
    // Neovim sets a global (g:) variable with the name we gave,
    // so we can get the result from an nvim_eval command.
    nvim.eval_cb("g:uniquevariable", [&](obj res, obj err) {
      REQUIRE(err.is_nil());
      REQUIRE(res.type == msgpack::type::POSITIVE_INTEGER);
      REQUIRE(res.as<int>() == 253);
      done = true;
    });
    wait_for_value(done, true);
  }
  SECTION("nvim_set_var works for strings")
  {
    std::atomic<bool> done = false;
    nvim.set_var("uniquevariabletwo", std::string("doesthiswork"));
    nvim.eval_cb("g:uniquevariabletwo", [&](obj res, obj err) {
      REQUIRE(err.is_nil());
      REQUIRE(res.type == msgpack::type::STR);
      REQUIRE(res.as<std::string>() == "doesthiswork");
      done = true;
    });
    wait_for_value(done, true);
  }
}
