#ifndef NVUI_CURSOR_HPP
#define NVUI_CURSOR_HPP
#include <QTimer>
#include <msgpack.hpp>
#include "hlstate.hpp"

/// The Cursor class stores the data for the Neovim cursor
/// 
class Cursor : public QObject
{
  Q_OBJECT
public:
  Cursor();
  /**
   * Handles a 'mode_info_set' Neovim redraw event.
   */
  void mode_info_set(const msgpack::object* obj, std::uint32_t size);
  /**
   * Handles a 'mode_change' Neovim redraw event.
   */
  void mode_change(const msgpack::object* obj, std::uint32_t size);
private:
  enum class CursorShape : std::uint8_t
  {
    Block,
    Horizontal,
    Vertical
  };
  enum class CursorStatus : std::uint8_t
  {
    Visible,
    Hidden
  };
  struct ModeInfo
  {
    CursorShape cursor_shape = CursorShape::Block;
    int cell_percentage = 0;
    int blinkwait = 0;
    int blinkon = 0;
    int blinkoff = 0;
    int attr_id = 0;
    int attr_id_lm;
    std::string short_name;
    std::string name;
    // (mouse_shape)
  };
  CursorStatus status = CursorStatus::Visible;
  QTimer blinkwait_timer;
  QTimer blinkon_timer;
  QTimer blinkoff_timer;
  int row = 0;
  int col = 0;
  std::vector<ModeInfo> mode_info;
  ModeInfo cur_mode;
  int cur_mode_idx;
  std::string cur_mode_name;
signals:
  void cursor_visible();
  void cursor_hidden();
private:
  /**
   * Stop/restart the timers.
   * This should be activated after the mode
   * has changed.
   */
  void reset_timers() noexcept;
  /**
   * Shows the cursor and sets the 'blinkon' timer,
   * which will start the 'blinkoff' timer when it
   * times out.
   */
  void set_blinkon_timer(int ms) noexcept;
  /**
   * Hides the cursor and starts the blinkoff
   * timer, which will start the 'blinkon' timer
   * when it times out.
   */
  void set_blinkoff_timer(int ms) noexcept;
  /**
   * Hide the cursor and set the status to Hidden.
   */
  void hide() noexcept;
  /**
   * Show the cursor and set the status to Visible.
   */
  void show() noexcept;
};

#endif // NVUI_CURSOR_HPP
