#include "animation.hpp"

Animation::Animation()
  : elapsed_timer(),
    timer(),
    animation_duration(-1.0),
    time_left(-1.0),
    stop_func()
{
}

bool Animation::is_running() const { return timer.isActive(); }
bool Animation::is_valid() const { return animation_duration >= 0; }
void Animation::set_duration(double dur) { animation_duration = dur; }
void Animation::set_interval(int ms) { timer.setInterval(ms); }

void Animation::reset()
{
  elapsed_timer.invalidate();
  timer.stop();
  stop_func();
  animation_duration = -1.0;
  time_left = -1.0;
}

void Animation::stop()
{
  timer.stop();
  if (stop_func) stop_func();
}

void Animation::start()
{
  if (animation_duration < 0) return;
  time_left = animation_duration;
  timer.start();
  elapsed_timer.start();
}

double Animation::percent_finished() const
{
  return 1 - (time_left / animation_duration);
}

void Animation::update_dt()
{
  time_left -= double(timer.interval()) / 1000.0;
}

void Animation::on_stop(std::function<void ()> stopfunc)
{
  stop_func = std::move(stopfunc);
}
