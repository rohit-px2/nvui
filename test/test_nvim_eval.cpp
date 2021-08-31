#include "nvim.hpp"
#include <catch2/catch.hpp>
#include <atomic>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

using obj = msgpack::object;
using obj_handle = msgpack::object_handle;
using namespace std::chrono_literals;

TEST_CASE("nvim_eval callbacks work", "[eval_cb]")
{
  Nvim nvim;
  REQUIRE(nvim.running());
  SECTION("Evaluating math")
  {
    std::atomic<bool> done = false;
    nvim.eval_cb("1 + 2", [&](obj res, obj err) {
      REQUIRE(err.is_nil());
      REQUIRE(res.type == msgpack::type::POSITIVE_INTEGER);
      REQUIRE(res.as<int>() == 3);
      done = true;
    });
    std::atomic_wait(&done, true);
  }
  SECTION("Can evaluate variables")
  {
    std::atomic<bool> done = false;
    nvim.eval_cb("stdpath('config')", [&](obj res, obj err) {
      REQUIRE(err.is_nil());
      REQUIRE(res.type == msgpack::type::STR);
      done = true;
    });
    std::atomic_wait(&done, true);
  }
  SECTION("Will send errors in the 'err' parameter")
  {
    std::atomic<bool> done = false;
    nvim.eval_cb("stdpath", [&](obj res, obj err) {
      REQUIRE(res.is_nil());
      REQUIRE(!err.is_nil());
      done = true;
    });
    std::atomic_wait(&done, true);
  }
}
