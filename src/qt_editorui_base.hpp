#ifndef NVUI_QT_EDITORUI_BASE_HPP
#define NVUI_QT_EDITORUI_BASE_HPP

#include "editor_base.hpp"
#include "mouse.hpp"
#include "scalers.hpp"
#include "types.hpp"
#include <QInputMethod>

class QKeyEvent;
class QInputMethodEvent;
class QMouseEvent;
class QWheelEvent;
class QDropEvent;
class QResizeEvent;
class QDragEnterEvent;

struct UISignaller : public QObject
{
  Q_OBJECT
signals:
  void titlebar_set(bool set);
  void titlebar_toggled();
  void frame_set(bool set);
  void frame_toggled();
  void title_changed(QString title);
  void window_opacity_changed(double opacity);
  void fullscreen_set(bool fullscreen);
  void fullscreen_toggled();
  void titlebar_font_family_set(QString family);
  void titlebar_font_size_set(double pointsize);
  void titlebar_colors_unset();
  void titlebar_fg_set(QColor fg);
  void titlebar_bg_set(QColor bg);
  void titlebar_fg_bg_set(QColor fg, QColor bg);
  void default_colors_changed(QColor fg, QColor bg);
  void closed();
};

// Does not inherit from a widget type,
// so inheritors can inherit from
// QtEditorUIBase and another QWidget-based type
struct QtEditorUIBase : public EditorBase
{
  using Base = EditorBase;
public:
  QtEditorUIBase(
    QWidget& inheritor_instance,
    int cols,
    int rows,
    std::unordered_map<std::string, bool> capabilities,
    std::string nvim_path,
    std::vector<std::string> nvim_args
  );
  ~QtEditorUIBase() override = default;
  void attach();
  void setup() override;
  virtual void set_animations_enabled(bool enabled);
  float charspacing() const;
  float linespacing() const;
  float move_animation_duration() const;
  int move_animation_frametime() const;
  float scroll_animation_duration() const;
  int scroll_animation_frametime() const;
  float cursor_animation_duration() const;
  int cursor_animation_frametime() const;
  bool animations_enabled() const;
  u32 snapshot_limit() const;
  // Connect to the UI signaller to receieve
  // signals
  // We have to do this since if this class
  // inherits from QObject there will be ambiguity
  // since QWidget inherits from QObject
  UISignaller* ui_signaller();
protected:
  bool idling() const;
  // Handling UI events.
  // NOTE: Does not handle hiding the mouse
  // and delegating key presses to other widgets,
  // that must be done by the widget's event handler
  void handle_key_press(QKeyEvent*);
  QVariant handle_ime_query(Qt::InputMethodQuery);
  void handle_ime_event(QInputMethodEvent*);
  void handle_nvim_resize(QResizeEvent*);
  void handle_mouse_press(QMouseEvent*);
  void handle_mouse_move(QMouseEvent*);
  void handle_mouse_release(QMouseEvent*);
  void handle_wheel(QWheelEvent*);
  void handle_drop(QDropEvent*);
  void handle_drag(QDragEnterEvent*);
  void listen_for_notification(
    std::string method,
    std::function<void (const ObjectArray&)> func
  );
  virtual void linespace_changed(float new_ls) = 0;
  virtual void charspace_changed(float new_cs) = 0;
private:
  void cursor_moved() override;
  void typed();
  void unhide_cursor();
  void field_updated(std::string_view field, const Object& value) override;
  // Emits UISignaller::default_colors_changed
  void default_colors_changed(Color, Color) override;
  // Emits UISignaller::closed
  void do_close() override;
  struct GridPos
  {
    int grid_num;
    int row;
    int col;
  };
  /**
   * Get the grid num, row, and column for the given
   * (x, y) pixel position.
   * Returns nullopt if no grid could be found that
   * matches the requirements.
   */
  std::optional<GridPos> grid_pos_for(QPoint pos) const;
  void send_mouse_input(
    QPoint pos,
    std::string btn,
    std::string action,
    std::string mods
  );
  void register_command_handlers();
  void idle();
  void un_idle();
  void set_scaler(scalers::time_scaler& sc, const std::string& name);
protected:
  QWidget& inheritor;
  struct AnimationDetails
  {
    void set_interval(int ms)
    {
      if (ms < 0) return;
      ms_interval = ms;
    }
    void set_duration(float dur)
    {
      if (dur < 0) return;
      duration = dur;
    }
    int ms_interval;
    float duration;
  };
  struct IdleState
  {
    bool were_animations_enabled;
  };
  struct UIInformation
  {
    int cols;
    int rows;
    std::unordered_map<std::string, bool> capabilities;
  };
  UIInformation ui_attach_info;
  bool animate = true;
  bool mousehide = false;
  float charspace = 0;
  float linespace = 0;
  u32 snapshot_count = 4;
  AnimationDetails move_animation {4, 0.5f};
  AnimationDetails scroll_animation {10, 0.3f};
  AnimationDetails cursor_animation {10, 0.3f};
  QTimer idle_timer {};
  bool should_idle = false;
  std::optional<IdleState> idle_state;
  Mouse mouse;
  // This class is responsible for emitting signals
  // so that QtEditorUIBase doesn't have to inherit from QObject
  UISignaller signaller;
};

#endif // NVUI_QT_EDITORUI_BASE_HPP
