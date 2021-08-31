#include "cursor.hpp"
#include "editor.hpp"
#include "grid.hpp"
#include <fmt/core.h>
#include <fmt/format.h>
#include <QElapsedTimer>

using scalers::time_scaler;
time_scaler Cursor::animation_scaler = scalers::oneminusexpo2negative10;

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

Cursor::Cursor(EditorArea* ea)
  : Cursor()
{
  assert(ea);
  editor_area = ea;
  cursor_animation_timer.callOnTimeout([this] {
    auto elapsed_ms = elapsed_timer.elapsed();
    cursor_animation_time -= static_cast<float>(elapsed_ms) / 1000.f;
    if (cursor_animation_time <= 0.f)
    {
      cursor_animation_timer.stop();
      cur_x = destination_x;
      cur_y = destination_y;
    }
    else
    {
      auto x_diff = destination_x - old_x;
      auto y_diff = destination_y - old_y;
      auto duration = editor_area->cursor_animation_duration();
      auto animation_left = cursor_animation_time / duration;
      float animation_finished = 1.0f - animation_left;
      float scaled = animation_scaler(animation_finished);
      cur_x = old_x + (x_diff * scaled);
      cur_y = old_y + (y_diff * scaled);
    }
    editor_area->update();
    elapsed_timer.start();
  });
}

void Cursor::mode_change(const msgpack::object* obj, std::uint32_t size)
{
  Q_UNUSED(size);
  assert(obj->type == msgpack::type::ARRAY);
  const auto& arr = obj->via.array;
  assert(arr.size == 2);
  const std::string mode_name = arr.ptr[0].as<std::string>();
  // Save the old position
  if (cur_pos.has_value()) old_mode_idx = cur_mode_idx;
  cur_mode_idx = arr.ptr[1].as<std::size_t>();
  if (cur_mode_idx >= mode_info.size())
  {
    return;
  }
  cur_mode = mode_info.at(cur_mode_idx);
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
    // const bool cursor_style_enabled = arr.ptr[0].as<bool>();
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
      mode_info.push_back(std::move(mode));
    }
  }
}

void Cursor::reset_timers() noexcept
{
  if (busy()) return;
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
  if (status != CursorStatus::Hidden && !busy())
  {
    emit cursor_hidden();
    status = CursorStatus::Hidden;
  }
}

void Cursor::show() noexcept
{
  if (status != CursorStatus::Visible && !busy())
  {
    emit cursor_visible();
    status = CursorStatus::Visible;
  }
}

void Cursor::go_to(CursorPos pos)
{
  prev_pos = cur_pos;
  if (!use_animated_position())
  {
    cursor_animation_timer.stop();
    cur_pos = pos;
  }
  else
  {
    cur_pos = pos;
    old_x = cur_x;
    old_y = cur_y;
    destination_x = cur_pos->grid_x + cur_pos->col;
    destination_y = cur_pos->grid_y + cur_pos->row;
    cursor_animation_time = editor_area->cursor_animation_duration();
    auto interval = editor_area->cursor_animation_frametime();
    elapsed_timer.start();
    if (cursor_animation_timer.interval() != interval)
    {
      cursor_animation_timer.setInterval(interval);
    }
    if (!cursor_animation_timer.isActive()) cursor_animation_timer.start();
  }
  reset_timers();
}

/// Returns a CursorRect containing the cursor rectangle
/// in pixels, based on the row, column, font width, and font height.
static CursorRect get_rect(
  const ModeInfo& mode,
  float row,
  float col,
  float font_width,
  float font_height,
  float caret_extend_top,
  float caret_extend_bottom,
  float scale = 1.0f
)
{
  // These do nothing for now
  bool should_draw_text = mode.cursor_shape == CursorShape::Block;
  /// Top left coordinates.
  QPointF top_left = {col * font_width, row * font_height};
  QRectF rect;
  /// Depending on the cursor shape, the rectangle it occupies will be different
  /// Block and Underline shapes' width will change depending on the scale factor,
  /// but not the vertical shape. 
  switch(mode.cursor_shape)
  {
    case CursorShape::Block:
    {
      rect = {top_left.x(), top_left.y(), font_width * scale, font_height};
      break;
    }
    case CursorShape::Vertical:
    {
      // Rectangle starts at top_left, with a lower width.
      float width = (font_width * mode.cell_percentage) / 100.f;
      rect = {top_left.x(), top_left.y() - caret_extend_top, width, font_height + caret_extend_top+ caret_extend_bottom};
      break;
    }
    case CursorShape::Horizontal:
    {
      float height = (font_height * mode.cell_percentage) / 100.f;
      float start_y = top_left.y() + font_height - height;
      rect = {top_left.x(), start_y, font_width * scale, height};
      break;
    }
  }
  return {rect, mode.attr_id, should_draw_text};
}

std::optional<CursorRect> Cursor::rect(float font_width, float font_height, float scale) const noexcept
{
  if (!cur_pos.has_value()) return std::nullopt;
  float x = cur_pos->grid_x + cur_pos->col;
  float y = cur_pos->grid_y + cur_pos->row;
  if (use_animated_position())
  {
    x = cur_x;
    y = cur_y;
  }
  return get_rect(
    cur_mode,
    y,
    x,
    font_width, font_height,
    caret_extend_top,
    caret_extend_bottom,
    scale
  );
}

std::optional<CursorRect> Cursor::old_rect(float font_width, float font_height) const noexcept
{
  if (!prev_pos.has_value()) return std::nullopt;
  const auto& old_mode = mode_info.at(old_mode_idx);
  return get_rect(
    old_mode,
    prev_pos->grid_y + prev_pos->row,
    prev_pos->grid_x + prev_pos->col,
    font_width,
    font_height,
    caret_extend_top,
    caret_extend_bottom,
    old_mode_scale
  );
}

void Cursor::busy_start()
{
  hide();
  status = CursorStatus::Busy;
}

void Cursor::busy_stop()
{
  status = CursorStatus::Visible;
  reset_timers();
}

bool Cursor::use_animated_position() const
{
  return editor_area
      && editor_area->animations_enabled()
      && editor_area->cursor_animation_frametime() > 0;
}
