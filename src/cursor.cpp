#include "cursor.hpp"
#include <fmt/core.h>
#include <fmt/format.h>
Cursor::Cursor()
  : blinkwait_timer(nullptr),
    blinkon_timer(nullptr),
    blinkoff_timer(nullptr),
    mode_info(10)
{
  blinkwait_timer.setSingleShot(true);
  blinkon_timer.setSingleShot(true);
  blinkoff_timer.setSingleShot(true);
  QObject::connect(&blinkwait_timer, &QTimer::timeout, [this]() {
    hide();
    set_blinkoff_timer(cur_mode.blinkwait);
  });
  QObject::connect(&blinkon_timer, &QTimer::timeout, [this]() {
    hide();
    set_blinkoff_timer(cur_mode.blinkoff);
  });
  QObject::connect(&blinkoff_timer, &QTimer::timeout, [this]() {
    show();
    set_blinkon_timer(cur_mode.blinkon);
  });
}

void Cursor::mode_change(const msgpack::object* obj, std::uint32_t size)
{
  assert(obj->type == msgpack::type::ARRAY);
  const auto& arr = obj->via.array;
  assert(arr.size == 2);
  const std::string mode_name = arr.ptr[0].as<std::string>();
  const std::size_t mode_idx = arr.ptr[1].as<std::size_t>();
  if (mode_idx >= mode_info.size())
  {
    return;
  }
  cur_mode = mode_info.at(mode_idx);
  reset_timers();
}

void Cursor::mode_info_set(const msgpack::object* obj, std::uint32_t size)
{
  mode_info.clear();
  for(std::uint32_t i = 0; i < size; ++i)
  {
    const msgpack::object& o = obj[i];
    assert(o.type == msgpack::type::ARRAY);
    const auto& arr = o.via.array;
    assert(arr.size == 2);
    const bool cursor_style_enabled = arr.ptr[0].as<bool>();
    const auto& modes_arr = arr.ptr[1].via.array;
    for(std::uint32_t j = 0; j < modes_arr.size; ++j)
    {
      const msgpack::object_map& map = modes_arr.ptr[j].via.map;
      ModeInfo mode {};
      for(std::uint32_t k = 0; k < map.size; ++k)
      {
        const msgpack::object_kv kv = map.ptr[k];
        const std::string key = kv.key.as<std::string>();
        if (key == "cursor_shape")
        {
          const std::string shape = kv.val.as<std::string>();
          if (shape == "horizontal")
          {
            mode.cursor_shape = CursorShape::Horizontal;
          }
          else if (shape == "vertical")
          {
            mode.cursor_shape = CursorShape::Vertical;
          }
          else
          {
            mode.cursor_shape = CursorShape::Block;
          }
        }
        else if (key == "cell_percentage")
        {
          mode.cell_percentage = kv.val.as<int>();
        }
        else if (key == "attr_id")
        {
          mode.attr_id = kv.val.as<int>();
        }
        else if (key == "attr_id_lm")
        {
          mode.attr_id_lm = kv.val.as<int>();
        }
        else if (key == "short_name")
        {
          mode.short_name = kv.val.as<std::string>();
        }
        else if (key == "name")
        {
          mode.name = kv.val.as<std::string>();
        }
        else if (key == "blinkwait")
        {
          mode.blinkwait = kv.val.as<int>();
        }
        else if (key == "blinkon")
        {
          mode.blinkon = kv.val.as<int>();
        }
        else if (key == "blinkoff")
        {
          mode.blinkoff = kv.val.as<int>();
        }
      }
      mode_info.push_back(mode);
    }
  }
}

void Cursor::reset_timers() noexcept
{
  show();
  blinkwait_timer.stop();
  blinkoff_timer.stop();
  blinkon_timer.stop();
  // Blinking works like this:
  // First of all, if any of the numbers are 0, then there is no blinking.
  if (cur_mode.blinkwait == 0 || cur_mode.blinkoff == 0 || cur_mode.blinkon == 0) return;
  // 1. Cursor starts in a solid (visible) state.
  // 2. Cursor stays that way for 'blinkon' ms.
  // 3. After 'blinkon' ms, the cursor becomes hidden
  // 4. Cursor stays that way for 'blinkoff' ms.
  // 5. After 'blinkoff' ms, the cursor becomes visible.
  // 6. Repeat

  // On cursor move,
  // 1. The cursor immediately becomes visible.
  // 2. The cursor stays that way for 'blinkwait' ms.
  // 3. After 'blinkwait' ms, the cursor becomes hidden.
  // 4. Repeat the above blinking steps, but starting from step 4.
  blinkwait_timer.start(cur_mode.blinkwait);
}

void Cursor::set_blinkoff_timer(int ms) noexcept
{
  blinkoff_timer.start(ms);
}

void Cursor::set_blinkon_timer(int ms) noexcept
{
  blinkon_timer.start(ms);
}

void Cursor::hide() noexcept
{
  if (status != CursorStatus::Hidden)
  {
    emit cursor_hidden();
    status = CursorStatus::Hidden;
  }
}

void Cursor::show() noexcept
{
  if (status != CursorStatus::Visible)
  {
    emit cursor_visible();
    status = CursorStatus::Visible;
  }
}
