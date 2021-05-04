#include "nvim.hpp"
#include <catch2/catch.hpp>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

void sleep(std::uint32_t ms)
{
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

TEST_CASE("nvim_set_var sets variables properly", "[nvim_set_var]")
{
  // One thing to note: nvim_set_var only works for setting strings and ints
  // (as is said in the header file). However it's just a template method
  // so adding more types is as easy as adding more declarations in the cpp file.
  auto nvim = std::make_shared<Nvim>();
  REQUIRE(nvim->running());
  SECTION("nvim_set_var works for ints")
  {
    bool success = false;
    nvim->set_var("uniquevariable", 253);
    // Since we send a notification, we don't get a response (although the 
    // function doesn't send a response anyway).
    // We'll just wait a little bit and check. If the variable is still not
    // set then there's a performance issue, which means something else is wrong.
    sleep(20); // 0.02s
    try
    {
      const auto result = nvim->eval("g:uniquevariable").get().as<int>();
      if (result == 253)
      {
        success = true;
      }
    }
    catch(const std::exception& e) {}
    REQUIRE(success);
  }
  SECTION("nvim_set_var works for strings")
  {
    bool success = false;
    nvim->set_var("uniquevariabletwo", std::string("doesthiswork"));
    sleep(20);
    try
    {
      const auto result = nvim->eval("g:uniquevariabletwo").get().as<std::string>();
      if (result == "doesthiswork")
      {
        success = true;
      }
    }
    catch (const std::exception& e) {}
    REQUIRE(success);
  }
}
