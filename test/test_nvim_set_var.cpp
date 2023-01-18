#include "nvim.hpp"
#include "utils.hpp"
#include <catch2/catch_test_macros.hpp>
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
  // One thing to note: nvim_set_var only works for setting strings and ints
  // (as is said in the header file). However it's just a template method
  // so adding more types is as easy as adding more declarations in the cpp file.
  Nvim nvim;
  REQUIRE(nvim.running());
  SECTION("nvim_set_var works for ints")
  {
    std::atomic<bool> done = false;
    nvim.set_var("uniquevariable", 253);
    // Neovim sets a global (g:) variable with the name we gave,
    // so we can get the result from an nvim_eval command.
    nvim.eval_cb("g:uniquevariable", [&](Object res, Object err) {
      REQUIRE(err.is_null());
      auto result = res.try_convert<int>();
      REQUIRE(result);
      REQUIRE(*result == 253);
      done = true;
    });
    wait_for_value(done, true);
  }
  SECTION("nvim_set_var works for strings")
  {
    std::atomic<bool> done = false;
    nvim.set_var("uniquevariabletwo", std::string("doesthiswork"));
    nvim.eval_cb("g:uniquevariabletwo", [&](Object res, Object err) {
      REQUIRE(err.is_null());
      auto str = res.string();
      REQUIRE(str);
      REQUIRE(*str == "doesthiswork");
      done = true;
    });
    wait_for_value(done, true);
  }
}
