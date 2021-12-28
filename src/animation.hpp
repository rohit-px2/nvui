#ifndef NVUI_ANIMATION_HPP
#define NVUI_ANIMATION_HPP

#include <QElapsedTimer>
#include <QTimer>

class Animation
{
public:
  Animation();
  bool is_running() const;
  /// Will it do anything if you start it
  bool is_valid() const;
  void reset();
  void start();
  void set_duration(double secs);
  void set_interval(int ms);
  template<typename Func>
  void on_update(Func&& f);
  double percent_finished() const;
  double duration() const;
  int interval() const;
  void on_stop(std::function<void ()> stopfunc);
  void stop();
private:
  void update_dt();
  QElapsedTimer elapsed_timer;
  QTimer timer;
  double animation_duration;
  double time_left;
  std::function<void ()> stop_func;
};

template<typename Func>
void Animation::on_update(Func&& f)
{
  timer.callOnTimeout([f = std::forward<Func>(f), this] {
    update_dt();
    if (time_left < 0)
    {
      stop();
      return;
    }
    f();
    elapsed_timer.start();
  });
}
#endif // NVUI_ANIMATION_HPP
