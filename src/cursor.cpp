#include "cursor.hpp"
#include "editor.hpp"
#include "grid.hpp"
#include "nvim_utils.hpp"
#include <fmt/core.h>
#include <fmt/format.h>
#include <QElapsedTimer>

using scalers::time_scaler;
time_scaler Cursor::animation_scaler = scalers::oneminusexpo2negative10;

time_scaler Cursor::effect_ease_func = scalers::identity;

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
  init_animations();
}


// Since for some of the effect animations
// they get split 50/50 between going up and down,
// we normalize it
static float cursor_effect_normalize(double t)
{
  if (t < 0.2 || t > 0.8) return 1.f;
  else if (t < 0.5)
  {
    return (t * (-10.f / 3.f)) + (5.f / 3.f);
  }
  else
  {
    return (10.f / 3.f) * (t - 0.5f);
  }
}

void Cursor::register_nvim(Nvim& nvim)
{
  const auto on = [&](auto&&... p) {
    listen_for_notification(nvim, p..., this);
  };
  on("NVUI_CURSOR_SCALER",
    paramify<std::string>([](std::string scaler) {
      if (!scalers::scalers().contains(scaler)) return;
      Cursor::animation_scaler = scalers::scalers().at(scaler);
  }));
  on("NVUI_CURSOR_ANIMATION_DURATION",
    paramify<float>([this](float s) {
      move_animation.set_duration(s);
  }));
  on("NVUI_CURSOR_FRAMETIME",
    paramify<int>([this](int ms) {
      move_animation.set_interval(ms);
  }));
  on("NVUI_CURSOR_EFFECT",
    paramify<std::string>([this](std::string eff) {
      set_effect(eff);
  }));
  on("NVUI_CURSOR_EFFECT_FRAMETIME",
    paramify<int>([this](int ms) {
      if (ms <= 0) set_effect("none");
      set_effect_anim_frametime(ms);
  }));
  on("NVUI_CURSOR_EFFECT_DURATION",
    paramify<double>([this](double secs) {
      if (secs <= 0) set_effect("none");
      set_effect_anim_duration(secs);
  }));
  on("NVUI_CURSOR_EFFECT_SCALER",
    paramify<std::string>([this](std::string scaler) {
      set_effect_ease_func(scaler);
    })
  );
  on("NVUI_CARET_EXTEND", paramify<float, float>([this](float top, float bot) {
    set_caret_extend(top, bot);
  }));
  on("NVUI_CARET_EXTEND_TOP", paramify<float>([this](float caret_top) {
    set_caret_extend_top(caret_top);
  }));
  on("NVUI_CARET_EXTEND_BOTTOM", paramify<float>([this](float bot) {
    set_caret_extend_bottom(bot);
  }));
  using namespace std;
  handle_request<vector<string>, int>(nvim, "NVUI_CURSOR_EFFECT_SCALERS",
    [&](const auto&) {
      return tuple {scalers::scaler_names(), std::nullopt};
  }, this);
}

void Cursor::set_animations_enabled(bool enable)
{
  use_anims = enable;
}

bool Cursor::animations_enabled() const
{
  return use_anims;
}

void Cursor::init_animations()
{
  move_animation.on_update([this] {
    auto finished = move_animation.percent_finished();
    auto scaled = animation_scaler(finished);
    cur_x = old_x + (destination_x - old_x) * scaled;
    cur_y = old_y + (destination_y - old_y) * scaled;
    emit anim_state_changed();
  });
  move_animation.on_stop([this] {
    cur_x = destination_x;
    cur_y = destination_y;
    emit anim_state_changed();
  });
  effect_animation.on_update([this] {
    auto percent_finished = effect_animation.percent_finished();
    // When animation starts, opacity is at 1
    switch(cursor_effect)
    {
      case CursorEffect::SmoothBlink:
        animate_smoothblink(cursor_effect_normalize(percent_finished));
        break;
      case CursorEffect::ExpandShrink:
        animate_expandshrink(cursor_effect_normalize(percent_finished));
        break;
      default: return;
    }
    emit anim_state_changed();
  });
  effect_animation.on_stop([this] {
    opacity_level = 1.0;
    switch(cursor_effect)
    {
      case CursorEffect::SmoothBlink:
      case CursorEffect::ExpandShrink:
        if (!animations_enabled()) return;
        // continuous
        effect_animation.start();
        break;
      default: break;
    }
    emit anim_state_changed();
  });
  effect_animation.set_duration(1);
  effect_animation.set_interval(16);
  move_animation.set_duration(0.3);
  move_animation.set_interval(10);
}

void Cursor::animate_smoothblink(double percent_finished)
{
  // During the animation opacity goes like this:
  opacity_level = effect_ease_func(percent_finished);
}

void Cursor::animate_expandshrink(double percent_finished)
{
  height_level = effect_ease_func(percent_finished);
}

void Cursor::mode_change(std::span<const Object> objs)
{
  for(const auto& o : objs)
  {
    auto* arr = o.array();
    if (!(arr && arr->size() >= 2)) continue;
    const auto mode_name = arr->at(0).get<std::string>();
    if (cur_pos) old_mode_idx = cur_mode_idx;
    if (!arr->at(1).u64()) continue;
    cur_mode_idx = *arr->at(1).u64();
    cur_mode = mode_info.at(cur_mode_idx);
  }
  reset_timers();
}

void Cursor::mode_info_set(std::span<const Object> objs)
{
  mode_info.clear();
  for(const auto& o : objs)
  {
    auto* arr = o.array();
    if (!(arr && arr->size() >= 2)) continue;
    const auto modes_arr = arr->at(1).array();
    if (!modes_arr) continue;
    for(const auto& m : *modes_arr)
    {
      if (!m.has<ObjectMap>()) continue;
      const auto& map = *m.map();
      ModeInfo mode {};
      for(const auto& [key, val] : map)
      {
        if (key == "cursor_shape")
        {
          if (!val.string()) continue;
          const auto shape = *val.string();
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
          mode.cell_percentage = (int) val;
        }
        else if (key == "attr_id")
        {
          mode.attr_id = (int) val;
        }
        else if (key == "attr_id_lm")
        {
          mode.attr_id_lm = (int) val;
        }
        else if (key == "short_name")
        {
          mode.short_name = *val.string();
        }
        else if (key == "name")
        {
          mode.name = *val.string();
        }
        else if (key == "blinkwait")
        {
          mode.blinkwait = (int) val;
        }
        else if (key == "blinkon")
        {
          mode.blinkon = (int) val;
        }
        else if (key == "blinkoff")
        {
          mode.blinkoff = (int) val;
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
    move_animation.stop();
    cur_pos = pos;
  }
  else
  {
    cur_pos = pos;
    old_x = cur_x;
    old_y = cur_y;
    destination_x = cur_pos->grid_x + cur_pos->col;
    destination_y = cur_pos->grid_y + cur_pos->row;
    cursor_animation_time = move_animation.duration();
    move_animation.start();
    switch(cursor_effect)
    {
      case CursorEffect::SmoothBlink:
      case CursorEffect::ExpandShrink:
        effect_animation.start();
        break;
      default: break;
    }
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
  return {rect, mode.attr_id, should_draw_text, 1.0f};
}

std::optional<CursorRect> Cursor::rect(
  float font_width,
  float font_height,
  float scale,
  bool varheight
) const noexcept
{
  if (!cur_pos.has_value()) return std::nullopt;
  float x = cur_pos->grid_x + cur_pos->col;
  float y = cur_pos->grid_y + cur_pos->row;
  if (use_animated_position())
  {
    x = cur_x;
    y = cur_y;
  }
  auto crect = get_rect(
    cur_mode,
    y,
    x,
    font_width, font_height,
    caret_extend_top,
    caret_extend_bottom,
    scale
  );
  if (!varheight) return crect;
  switch(cursor_effect)
  {
    case CursorEffect::ExpandShrink:
    {
      auto cursor_height = crect.rect.height() * height_level;
      float y_off = (crect.rect.height() - cursor_height) / 2.0;
      crect.rect.setY(crect.rect.y() + y_off);
      crect.rect.setHeight(cursor_height);
      break;
    }
    case CursorEffect::SmoothBlink:
      crect.opacity = opacity();
      break;
    default: break;
  }
  return crect;
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
  return animations_enabled() && move_animation.interval() > 0;
}

double Cursor::opacity() const { return opacity_level; }

void Cursor::set_effect(std::string_view eff)
{
  bool valid_eff = true;
  effect_animation.stop();
  if (eff == "smoothblink")
  {
    cursor_effect = CursorEffect::SmoothBlink;
  }
  else if (eff == "expandshrink")
  {
    cursor_effect = CursorEffect::ExpandShrink;
  }
  else
  {
    cursor_effect = CursorEffect::NoEffect;
    valid_eff = false;
  }
  if (valid_eff) effect_animation.start();
}

void Cursor::set_effect_anim_duration(double dur)
{
  effect_animation.set_duration(dur);
}

void Cursor::set_effect_anim_frametime(int ms)
{
  effect_animation.set_interval(ms);
}

void Cursor::set_effect_ease_func(std::string_view funcname)
{
  std::string fname {funcname};
  if (scalers::scalers().contains(fname))
  {
    effect_ease_func = scalers::scalers().at(fname);
  }
  else
  {
    effect_ease_func = scalers::identity;
  }
}
