#ifndef NVUI_MOUSE_HPP
#define NVUI_MOUSE_HPP

#include <QTimer>
#include <QMouseEvent>

struct Mouse
{
  Mouse() = default;
  Mouse(int interval)
    : click_timer(),
      gridid(),
      row(), col(),
      click_interval(interval)
  {
    click_timer.setSingleShot(true);
    click_timer.setInterval(interval);
    click_timer.callOnTimeout([&] {
      reset_click();
    });
  }
  void button_clicked(Qt::MouseButton b)
  {
    if (cur_button == b)
    {
      ++click_count;
      start_timer();
    }
    else
    {
      reset_click();
      cur_button = b;
      click_count = 1;
      start_timer();
    }
  }
  void reset_click()
  {
    click_timer.stop();
    click_count = 0;
    cur_button = Qt::NoButton;
  }
  void start_timer()
  {
    if (click_timer.isActive()) return;
    click_timer.start();
  }
  int click_count = 0;
  Qt::MouseButton cur_button = Qt::NoButton;
  QTimer click_timer;
  int gridid = 0;
  int row = 0;
  int col = 0;
  int click_interval;
};

#endif // NVUI_MOUSE_HPP
