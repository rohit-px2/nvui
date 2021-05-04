#include "nvim.hpp"
#include <catch2/catch.hpp>
#include <memory>
#include <string>

TEST_CASE("nvim_eval works", "[nvim_eval]")
{
  auto nvim = std::make_shared<Nvim>();
  REQUIRE(nvim->running());
  SECTION("nvim_eval works for evaluating math")
  {
    const auto math = nvim->eval("1 + 2").get().as<std::int32_t>();
    REQUIRE(math == 3);
  }
  SECTION("nvim_eval can evaluate variables")
  {
    // Everyone can have a different config path,
    // so we just check that it's not empty
    const auto config = nvim->eval("stdpath('config')").get().as<std::string>();
    REQUIRE(!config.empty());
  }
  SECTION("nvim_eval returns errors if things don't work out")
  {
    bool exception_occurred = false;
    try
    {
      // When we get an error the output reader gives us an array.
      // We should run into a type error when we try to convert it.
      const auto error = nvim->eval("stdpath('')").get().as<std::string>();
    }
    catch (const std::exception& e)
    {
      exception_occurred = true;
    }
    REQUIRE(exception_occurred);
  }
}
