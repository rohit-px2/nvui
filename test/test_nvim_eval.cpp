#include "nvim.hpp"
#include "utils.hpp"
#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include "object.hpp"

using namespace std::chrono_literals;

TEST_CASE("nvim_eval callbacks work", "[eval_cb]")
{
  Nvim nvim;
  REQUIRE(nvim.running());
  SECTION("Evaluating math")
  {
    std::atomic<bool> done = false;
    nvim.eval_cb("1 + 2", [&](Object res, Object err) {
      REQUIRE(err.is_null());
      REQUIRE(res.try_convert<int>());
      REQUIRE(*res.try_convert<int>() == 3);
      done = true;
    });
    wait_for_value(done, true);
  }
  SECTION("Can evaluate variables")
  {
    std::atomic<bool> done = false;
    nvim.eval_cb("stdpath('config')", [&](Object res, Object err) {
      REQUIRE(err.is_null());
      REQUIRE(res.string());
      done = true;
    });
    wait_for_value(done, true);
  }
  SECTION("Will send errors in the 'err' parameter")
  {
    std::atomic<bool> done = false;
    nvim.eval_cb("stdpath", [&](Object res, Object err) {
      REQUIRE(res.is_null());
      REQUIRE(!err.is_null());
      done = true;
    });
    wait_for_value(done, true);
  }
}
