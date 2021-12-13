#include "msgpack_overrides.hpp"
#include "window.hpp"
#include "utils.hpp"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <QObject>
#include <QLabel>
#include <QHBoxLayout>
#include <QPushButton>
#include <QStyle>
#include <QApplication>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <QIcon>
#include <QWindow>
#include <QSizeGrip>
#include <sstream>
#include <thread>
#include "constants.hpp"
#include "object.hpp"
#include <QApplication>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
/// Add margin to a HWND window
static void add_margin(HWND hwnd, MARGINS margins)
{
  DwmExtendFrameIntoClientArea(hwnd, &margins);
}
/// Remove margins from a HWND window
static void remove_margin(HWND hwnd)
{
  MARGINS margins {0, 0, 0, 0};
  DwmExtendFrameIntoClientArea(hwnd, &margins);
}
/// Setup the frameless window for aero snap, etc.
static void windows_setup_frameless(HWND hwnd)
{
  add_margin(hwnd, {0, 0, 1, 0});
  SetWindowLong(
    hwnd,
    GWL_STYLE,
    WS_THICKFRAME | WS_MINIMIZEBOX
    | WS_MAXIMIZEBOX | WS_CLIPCHILDREN | WS_CAPTION
  );
}
#endif

using u32 = std::uint32_t;

/**
 * Automatically unpack ObjectArray into the desired parameters,
 * or exit if it doesn't match.
 */
template<typename... T, typename Func>
static std::function<void (const ObjectArray&)> paramify(Func&& f)
{
  return [f](const ObjectArray& arg_list) {
    std::tuple<T...> t;
    constexpr std::size_t types_len = sizeof...(T);
    if (arg_list.size() < types_len) return;
    bool valid = true;
    std::size_t idx = 0;
    for_each_in_tuple(t, [&](auto& p) {
      using val_type = std::remove_reference_t<decltype(p)>;
      if constexpr(std::is_same_v<val_type, QString>)
      {
        std::optional<QString> s {};
        if (auto* o_s = arg_list.at(idx).string())
        {
          s = QString::fromStdString(*o_s);
        }
        if (!s) { valid = false; ++idx; return; }
        else p = std::move(s.value());
        ++idx;
        return;
      }
      std::optional<val_type> v = arg_list.at(idx).try_convert<val_type>();
      if (!v) { valid = false; return; }
      p = std::move(v.value());
      ++idx;
    });
    if (!valid) return;
    std::apply(f, t);
  };
}

Window::Window(QWidget* parent, Nvim* nv, int width, int height, bool custom_titlebar)
: QMainWindow(parent),
  semaphore(1),
  resizing(false),
  title_bar(std::make_unique<TitleBar>("nvui", this)),
  hl_state(),
  nvim(nv),
  editor_area(nullptr, &hl_state, nvim)
{
  assert(nv);
  assert(width > 0 && height > 0);
  setMouseTracking(true);
  QObject::connect(this, &Window::resize_done, &editor_area, &EditorArea::resized);
  prev_state = windowState();
  const auto [font_width, font_height] = editor_area.font_dimensions();
  resize(width * font_width, height * font_height);
  emit resize_done(size());
  if (custom_titlebar)
  {
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
#ifdef Q_OS_WIN
    windows_setup_frameless((HWND) winId());
#endif // Q_OS_WIN
  }
  else
  {
    title_bar->hide();
  }
  QObject::connect(title_bar.get(), &TitleBar::resize_move, this, &Window::resize_or_move);
  setWindowIcon(QIcon(constants::appicon()));
  // We'll do this later
  setCentralWidget(&editor_area);
  nvim->set_var("nvui_tb_separator", " • ");
  editor_area.setFocus();
  QObject::connect(this, &Window::default_colors_changed, [this] { update_titlebar_colors(); });
  QObject::connect(this, &Window::default_colors_changed, &editor_area, &EditorArea::default_colors_changed);
  QObject::connect(this, &Window::win_state_changed, title_bar.get(), &TitleBar::win_state_changed);
}

void Window::handle_redraw(Object redraw_args)
{
  auto* arr = redraw_args.array();
  assert(arr && arr->size() >= 3);
  auto* args = arr->at(2).array();
  assert(args);
  for(auto& o : *args)
  {
    auto* task = o.array();
    if (!task || task->size() == 0) continue;
    auto* task_name = task->at(0).string();
    if (!task_name) continue;
    const auto func_it = handlers.find(*task_name);
    if (func_it != handlers.end())
    {
      auto span = std::span {task->data() + 1, task->size() - 1};
      func_it->second(this, span);
    }
  }
}

void Window::set_handler(std::string method, obj_ref_cb handler)
{
  handlers[method] = handler;
}

static std::vector<std::string> scaler_names()
{
  std::vector<std::string> names;
  for(auto& sc : scalers::scalers()) names.push_back(sc.first);
  return names;
}

void Window::register_handlers()
{
  // Set GUI handlers before we set the notification handler (since Nvim runs on a different thread,
  // it can be called any time)
  set_handler("hl_attr_define", [](Window* w, std::span<const Object> objs) {
    for(const auto& obj : objs) w->hl_state.define(obj);
  });
  set_handler("hl_group_set", [](Window* w, std::span<const Object> objs) {
    for (const auto& obj : objs) w->hl_state.group_set(obj);
  });
  set_handler("default_colors_set", [](Window* w, std::span<const Object> objs) {
    if (objs.empty()) return;
    w->hl_state.default_colors_set(objs.back());
    const HLAttr& def_clrs = w->hl_state.default_colors_get();
    auto fg = def_clrs.fg()->qcolor();
    auto bg = def_clrs.bg()->qcolor();
    emit w->default_colors_changed(fg, bg);
  });
  set_handler("grid_line", [](Window* w, std::span<const Object> objs) {
    w->editor_area.grid_line(objs);
  });
  set_handler("option_set", [](Window* w, std::span<const Object> objs) {
    w->editor_area.option_set(objs);
  });
  set_handler("grid_resize", [](Window* w, std::span<const Object> objs) {
    w->editor_area.grid_resize(objs);
  });
  set_handler("flush", [](Window* w, std::span<const Object> objs) {
    Q_UNUSED(objs);
    w->editor_area.flush();
  });
  set_handler("win_pos", [](Window* w, std::span<const Object> objs) {
    w->editor_area.win_pos(objs);
  });
  set_handler("grid_clear", [](Window* w, std::span<const Object> objs) {
    w->editor_area.grid_clear(objs);
  });
  set_handler("grid_cursor_goto", [](Window* w, std::span<const Object> objs) {
    w->editor_area.grid_cursor_goto(objs);
  });
  set_handler("grid_scroll", [](Window* w, std::span<const Object> objs) {
    w->editor_area.grid_scroll(objs);
  });
  set_handler("mode_info_set", [](Window* w, std::span<const Object> objs) {
    w->editor_area.mode_info_set(objs);
  });
  set_handler("mode_change", [](Window* w, std::span<const Object> objs) {
    w->editor_area.mode_change(objs);
  });
  set_handler("popupmenu_show", [](Window* w, std::span<const Object> objs) {
    w->editor_area.popupmenu_show(objs);
  });
  set_handler("popupmenu_hide", [](Window* w, std::span<const Object> objs) {
    w->editor_area.popupmenu_hide(objs);
  });
  set_handler("popupmenu_select", [](Window* w, std::span<const Object> objs) {
    w->editor_area.popupmenu_select(objs);
  });
  set_handler("busy_start", [](Window* w, std::span<const Object> objs) {
    Q_UNUSED(objs);
    w->editor_area.busy_start();
  });
  set_handler("busy_stop", [](Window* w, std::span<const Object> objs) {
    Q_UNUSED(objs);
    w->editor_area.busy_stop();
  });
  set_handler("cmdline_show", [](Window* w, std::span<const Object> objs) {
    w->editor_area.cmdline_show(objs);
  });
  set_handler("cmdline_hide", [](Window* w, std::span<const Object> objs) {
    w->editor_area.cmdline_hide(objs);
  });
  set_handler("cmdline_pos", [](Window* w, std::span<const Object> objs) {
    w->editor_area.cmdline_cursor_pos(objs);
  });
  set_handler("cmdline_special_char", [](Window* w, std::span<const Object> objs) {
    w->editor_area.cmdline_special_char(objs);
  });
  set_handler("cmdline_block_show", [](Window* w, std::span<const Object> objs) {
    w->editor_area.cmdline_block_show(objs);
  });
  set_handler("cmdline_block_append", [](Window* w, std::span<const Object> objs) {
    w->editor_area.cmdline_block_append(objs);
  });
  set_handler("cmdline_block_hide", [](Window* w, std::span<const Object> objs) {
    w->editor_area.cmdline_block_hide(objs);
  });
  set_handler("mouse_on", [](Window* w, std::span<const Object> objs) {
    Q_UNUSED(objs);
    w->editor_area.set_mouse_enabled(true);
  });
  set_handler("mouse_off", [](Window* w, std::span<const Object> objs) {
    Q_UNUSED(objs);
    w->editor_area.set_mouse_enabled(false);
  });
  set_handler("win_hide", [](Window* w, std::span<const Object> objs) {
    w->editor_area.win_hide(objs);
  });
  set_handler("win_float_pos", [](Window* w, std::span<const Object> objs) {
    w->editor_area.win_float_pos(objs);
  });
  set_handler("win_close", [](Window* w, std::span<const Object> objs) {
    w->editor_area.win_close(objs);
  });
  set_handler("grid_destroy", [](Window* w, std::span<const Object> objs) {
    w->editor_area.grid_destroy(objs);
  });
  set_handler("msg_set_pos", [](Window* w, std::span<const Object> objs) {
    w->editor_area.msg_set_pos(objs);
  });
  set_handler("win_viewport", [](Window* w, std::span<const Object> objs) {
    w->editor_area.win_viewport(objs);
  });
  // The lambda will get invoked on the Nvim::read_output thread, we use
  // invokeMethod to then handle the data on our Qt thread.
  assert(nvim);
  nvim->set_notification_handler("redraw", [this](Object obj) {
    QMetaObject::invokeMethod(
      this, [this, o = std::move(obj)] {
        handle_redraw(std::move(o));
      }
    );
  });
  using notification = const ObjectArray&;
  listen_for_notification("NVUI_WINOPACITY", paramify<float>([this](double opacity) {
    if (opacity <= 0.0 || opacity > 1.0) return;
    setWindowOpacity(opacity);
  }));
  listen_for_notification("NVUI_TOGGLE_FRAMELESS", [this](notification params) {
    Q_UNUSED(params);
    if (is_frameless()) disable_frameless_window();
    else enable_frameless_window();
  });
  listen_for_notification("NVUI_CHARSPACE", paramify<float>([this](float space) {
    editor_area.set_charspace(space);
  }));
  listen_for_notification("NVUI_CARET_EXTEND", paramify<float, float>([this](float top, float bot) {
    editor_area.set_caret_top(top);
    editor_area.set_caret_bottom(bot);
  }));
  listen_for_notification("NVUI_CARET_EXTEND_TOP", paramify<float>([this](float caret_top) {
    editor_area.set_caret_top(caret_top);
  }));
  listen_for_notification("NVUI_CARET_EXTEND_BOTTOM", paramify<float>([this](float bot) {
    editor_area.set_caret_bottom(bot);
  }));
  listen_for_notification("NVUI_PUM_MAX_ITEMS", paramify<std::size_t>([this](std::size_t max_items) {
    editor_area.popupmenu_set_max_items(max_items);
  }));
  listen_for_notification("NVUI_PUM_MAX_CHARS", paramify<std::size_t>([this](std::size_t max_chars) {
    editor_area.popupmenu_set_max_chars(max_chars);
  }));
  listen_for_notification("NVUI_PUM_BORDER_WIDTH", paramify<std::size_t>([this](std::size_t b_width) {
    editor_area.popupmenu_set_border_width(b_width);
  }));
  listen_for_notification("NVUI_PUM_BORDER_COLOR", paramify<QString>([this](QString potential_clr) {
    if (!QColor::isValidColor(potential_clr)) return;
    QColor new_pum_border_color {potential_clr};
    editor_area.popupmenu_set_border_color(new_pum_border_color);
  }));
  listen_for_notification("NVUI_PUM_ICONS_TOGGLE", [this](notification params) {
    Q_UNUSED(params);
    editor_area.popupmenu_icons_toggle();
  });
  listen_for_notification("NVUI_PUM_ICON_OFFSET", paramify<int>([this](int offset) {
    editor_area.popupmenu_set_icon_size_offset(offset);
  }));
  listen_for_notification("NVUI_PUM_ICON_SPACING", paramify<float>([this](float spacing) {
    editor_area.popupmenu_set_icon_spacing(spacing);
  }));
  // :call rpcnotify(1, 'NVUI_PUM_ICON_FG', '(iname)', '(background color)')
  listen_for_notification("NVUI_PUM_ICON_BG",
    paramify<QString, QString>([this](QString icon_name, QString color_str) {
    if (!QColor::isValidColor(color_str)) return;
    editor_area.popupmenu_set_icon_bg(std::move(icon_name), {color_str});
  }));
  // :call rpcnotify(1, 'NVUI_PUM_ICON_FG', '(iname)', '(foreground color)')
  listen_for_notification("NVUI_PUM_ICON_FG",
    paramify<QString, QString>([this](QString icon_name, QString color_str) {
      if (!QColor::isValidColor(color_str)) return;
      editor_area.popupmenu_set_icon_fg(std::move(icon_name), {color_str});
  }));
  listen_for_notification("NVUI_PUM_ICON_COLORS",
    paramify<QString, QString, QString>([this](QString icon_name, QString fg_str, QString bg_str) {
      if (!QColor::isValidColor(fg_str) || !QColor::isValidColor(bg_str)) return;
      QColor fg {fg_str}, bg {bg_str};
      editor_area.popupmenu_set_icon_colors(icon_name, std::move(fg), std::move(bg));
  }));
  listen_for_notification("NVUI_PUM_DEFAULT_ICON_FG", paramify<QString>([this](QString fg_str) {
    if (!QColor::isValidColor(fg_str)) return;
    editor_area.popupmenu_set_default_icon_fg({fg_str});
  }));
  listen_for_notification("NVUI_PUM_DEFAULT_ICON_BG", paramify<QString>([this](QString bg_str) {
    if (!QColor::isValidColor(bg_str)) return;
    editor_area.popupmenu_set_default_icon_bg({bg_str});
  }));
  listen_for_notification("NVUI_PUM_ICONS_RIGHT", paramify<bool>([this](bool icons_on_right) {
    editor_area.popupmenu_set_icons_right(icons_on_right);
  }));
  listen_for_notification("NVUI_CMD_FONT_SIZE", paramify<float>([this](float size) {
    if (size <= 0.f) return;
    editor_area.cmdline_set_font_size(size);
  }));
  listen_for_notification("NVUI_CMD_BIG_SCALE", paramify<float>([this](float scale) {
    if (scale <= 0.f) return;
    editor_area.cmdline_set_font_scale_ratio(scale);
  }));
  listen_for_notification("NVUI_CMD_FONT_FAMILY", paramify<QString>([this](QString family) {
    editor_area.cmdline_set_font_family(family);
  }));
  listen_for_notification("NVUI_CMD_BG", paramify<QString>([this](QString bg_str) {
    if (!QColor::isValidColor(bg_str)) return;
    QColor bg {bg_str};
    editor_area.cmdline_set_bg(bg);
  }));
  listen_for_notification("NVUI_CMD_FG", paramify<QString>([this](QString fg_str) {
    if (!QColor::isValidColor(fg_str)) return;
    QColor fg {fg_str};
    editor_area.cmdline_set_fg(fg);
  }));
  listen_for_notification("NVUI_CMD_BORDER_WIDTH", paramify<int>([this](int width) {
    if (width < 0.f) return;
    editor_area.cmdline_set_border_width(width);
  }));
  listen_for_notification("NVUI_CMD_BORDER_COLOR", paramify<QString>([this](QString color_str) {
    if (!QColor::isValidColor(color_str)) return;
    QString color {color_str};
    editor_area.cmdline_set_border_color(color);
  }));
  listen_for_notification("NVUI_CMD_SET_LEFT", paramify<float>([this](float new_x) {
    if (new_x < 0.f || new_x > 1.f) return;
    editor_area.cmdline_set_x(new_x);
  }));
  listen_for_notification("NVUI_CMD_YPOS", paramify<float>([this](float new_y) {
    if (new_y < 0.f || new_y > 1.f) return;
    editor_area.cmdline_set_y(new_y);
  }));
  listen_for_notification("NVUI_CMD_WIDTH", paramify<float>([this](float new_width) {
    if (new_width < 0.f || new_width > 1.f) return;
    editor_area.cmdline_set_width(new_width);
  }));
  listen_for_notification("NVUI_CMD_HEIGHT", paramify<float>([this](float new_height) {
    if (new_height < 0.f || new_height > 1.f) return;
    editor_area.cmdline_set_height(new_height);
  }));
  listen_for_notification("NVUI_CMD_SET_CENTER_X", paramify<float>([this](float center_x) {
    if (center_x < 0.f || center_x > 1.f) return;
    editor_area.cmdline_set_center_x(center_x);
  }));
  listen_for_notification("NVUI_CMD_SET_CENTER_Y", paramify<float>([this](float center_y) {
    if (center_y < 0.f || center_y > 1.f) return;
    editor_area.cmdline_set_center_y(center_y);
  }));
  listen_for_notification("NVUI_CMD_PADDING", paramify<int>([this](int padding) {
    editor_area.cmdline_set_padding(padding);
  }));
  listen_for_notification("NVUI_FULLSCREEN", paramify<bool>([this](bool b) {
    set_fullscreen(b);
  }));
  listen_for_notification("NVUI_TOGGLE_FULLSCREEN", [this](auto) {
    if (isFullScreen()) set_fullscreen(false);
    else set_fullscreen(true);
  });
  listen_for_notification("NVUI_TITLEBAR_FONT_FAMILY", paramify<QString>([this](QString family) {
    title_bar->set_font_family(family);
    emit resize_done(size());
  }));
  listen_for_notification("NVUI_TITLEBAR_FONT_SIZE", paramify<double>([&](double sz) {
    auto prev = prev_state;
    auto state = windowState();
    title_bar->set_font_size(sz);
    // Editor area doesn't resize properly to fit unless you resize the window
    // with a different size
    resize(width() + 1, height() + 1);
    resize(width() - 1, height() - 1);
    setWindowState(state);
    // prev_state gets overriden, so we set it back to what it was before
    prev_state = prev;
  }));
  listen_for_notification("NVUI_TITLEBAR_COLORS",
    paramify<QString, QString>([this](QString fg, QString bg) {
      QColor fgc = fg, bgc = bg;
      if (!fgc.isValid() || !bgc.isValid()) return;
      titlebar_colors = {fgc, bgc};
      update_titlebar_colors();
  }));
  listen_for_notification("NVUI_TITLEBAR_UNSET_COLORS", [this](auto) {
    titlebar_colors.first.reset();
    titlebar_colors.second.reset();
    update_titlebar_colors();
  });
  listen_for_notification("NVUI_TITLEBAR_FG", paramify<QString>([this](QString fgs) {
    QColor fg = fgs;
    if (!fg.isValid()) return;
    titlebar_colors.first = fg;
    update_titlebar_colors();
  }));
  listen_for_notification("NVUI_TITLEBAR_BG", paramify<QString>([this](QString bgs) {
    QColor bg = bgs;
    if (!bg.isValid()) return;
    titlebar_colors.second = bg;
    update_titlebar_colors();
  }));
  listen_for_notification("NVUI_ANIMATION_FRAMETIME", paramify<int>([this](int ms) {
    editor_area.set_animation_frametime(ms);
  }));
  listen_for_notification("NVUI_ANIMATIONS_ENABLED", paramify<bool>([this](bool enabled) {
    editor_area.set_animations_enabled(enabled);
  }));
  listen_for_notification("NVUI_MOVE_ANIMATION_DURATION", paramify<float>([this](float s) {
    editor_area.set_move_animation_duration(s);
  }));
  listen_for_notification("NVUI_SCROLL_ANIMATION_DURATION", paramify<float>([this](float dur) {
    editor_area.set_scroll_animation_duration(dur);
  }));
  listen_for_notification("NVUI_SNAPSHOT_LIMIT", paramify<u32>([this](u32 limit) {
    editor_area.set_snapshot_count(limit);
  }));
  listen_for_notification("NVUI_SCROLL_FRAMETIME", paramify<int>([this](int ms) {
    editor_area.set_scroll_frametime(ms);
  }));
  listen_for_notification("NVUI_SCROLL_SCALER",
      paramify<std::string>([this](std::string s) {
        editor_area.set_scroll_scaler(s);
    }
  ));
  listen_for_notification("NVUI_MOVE_SCALER",
      paramify<QString>([this](QString s) {
        editor_area.set_move_scaler(s.toStdString());
  }));
  listen_for_notification("NVUI_PUM_INFO_COLS",
    paramify<int>([this](int cols) {
      editor_area.popupmenu_info_set_columns(cols);
  }));
  listen_for_notification("NVUI_FRAMELESS", paramify<bool>([this](bool b) {
    if (b) enable_frameless_window();
    else disable_frameless_window();
  }));
  listen_for_notification("NVUI_CURSOR_SCALER",
    paramify<std::string>([this](std::string scaler) {
      editor_area.set_cursor_scaler(scaler);
  }));
  listen_for_notification("NVUI_CURSOR_ANIMATION_DURATION",
    paramify<float>([this](float s) {
      editor_area.set_cursor_animation_duration(s);
  }));
  listen_for_notification("NVUI_CURSOR_FRAMETIME",
    paramify<int>([this](int ms) {
      editor_area.set_cursor_frametime(ms);
  }));
  listen_for_notification("NVUI_CURSOR_HIDE_TYPE",
    paramify<bool>([this](bool hide) {
      editor_area.set_cursor_hidden_while_typing(hide);
      editor_area.unsetCursor(); // Reset state.
  }));
  listen_for_notification("NVUI_TB_TITLE",
    paramify<QString>([this](QString text) {
      title_bar->set_title_text(text);
  }));
  listen_for_notification("NVUI_IME_SET",
    paramify<bool>([this](bool enable) {
      editor_area.setAttribute(Qt::WA_InputMethodEnabled, enable);
  }));
  listen_for_notification("NVUI_IME_TOGGLE", [&](const auto&) {
    constexpr auto ime = Qt::WA_InputMethodEnabled;
    if (editor_area.testAttribute(ime))
    {
      editor_area.setAttribute(ime, false);
    }
    else editor_area.setAttribute(ime, true);
  });
  listen_for_notification("NVUI_EXT_POPUPMENU",
    paramify<bool>([this](bool b) {
      nvim->ui_set_option("ext_popupmenu", b);
  }));
  listen_for_notification("NVUI_EXT_CMDLINE",
    paramify<bool>([this](bool b) {
      nvim->ui_set_option("ext_cmdline", b);
  }));
  listen_for_notification("NVUI_CURSOR_EFFECT",
    paramify<std::string>([this](std::string eff) {
      editor_area.cursor_set_effect(eff);
  }));
  listen_for_notification("NVUI_CURSOR_EFFECT_FRAMETIME",
    paramify<int>([this](int ms) {
      if (ms <= 0) editor_area.cursor_set_effect("none");
      else editor_area.cursor_set_effect_frametime(ms);
  }));
  listen_for_notification("NVUI_CURSOR_EFFECT_DURATION",
    paramify<double>([this](double secs) {
      if (secs <= 0) editor_area.cursor_set_effect("none");
      else editor_area.cursor_set_effect_duration(secs);
  }));
  listen_for_notification("NVUI_CURSOR_EFFECT_SCALER",
    paramify<std::string>([this](std::string scaler) {
      editor_area.cursor_set_effect_scaler(scaler);
    })
  );
  listen_for_notification("NVUI_IDLE_WAIT_FOR",
    paramify<double>([this](double seconds) {
      editor_area.set_idling_time(seconds);
  }));
  /// Add request handlers
  handle_request<std::vector<std::string>, std::string>(
    "NVUI_POPUPMENU_ICON_NAMES", [&](const ObjectArray& arr) {
      Q_UNUSED(arr);
      return std::tuple {editor_area.popupmenu_icon_list(), std::nullopt}; 
    }
  );
  handle_request<std::vector<std::string>, std::string>(
    "NVUI_SCALER_NAMES", [&](const ObjectArray&) {
      return std::tuple {scaler_names(), std::nullopt};
    }
  );
  handle_request<std::vector<std::string>, std::string>(
    "NVUI_CURSOR_EFFECT_NAMES", [&](const ObjectArray&) {
      using namespace std;
      return tuple {vector<string> {"smoothblink", "expandshrink", "none"}, std::nullopt};
    }
  );
  handle_request<std::vector<std::string>, std::string>(
    "NVUI_CURSOR_EFFECT_SCALERS", [&](const ObjectArray&) {
      return std::tuple {scaler_names(), std::nullopt};
    }
  );
  auto script_dir = constants::script_dir().toStdString();
  nvim->command(fmt::format("set rtp+={}", script_dir));
  nvim->command("runtime! plugin/nvui.vim");
  // helptags doesn't automatically update when the documentation has changed
  nvim->command(fmt::format("helptags {}", script_dir + "/doc"));
}

enum ResizeType
{
  NoResize = Qt::ArrowCursor,
  Horizontal = Qt::SizeHorCursor,
  Vertical = Qt::SizeVerCursor,
  NorthEast = Qt::SizeBDiagCursor,
  SouthWest = Qt::SizeFDiagCursor
};

static ResizeType should_resize(
  const QRect& win_rect,
  const int tolerance,
  const QMouseEvent* event
)
{
  const QRect inner_rect = QRect(
    win_rect.x() + tolerance, win_rect.y() + tolerance,
    win_rect.width() - 2 * tolerance, win_rect.height() - 2 * tolerance
  );
  const auto& pos = event->pos();
  //std::cout << "\nx: " << pos.x() << ", y: " << pos.y() << '\n';
  if (inner_rect.contains(pos))
  {
    return ResizeType::NoResize;
  }
  const int width = win_rect.width();
  const int height = win_rect.height();
  const int mouse_x = pos.x();
  const int mouse_y = pos.y();
  if (mouse_x > tolerance && mouse_x < (width - tolerance))
  {
    return ResizeType::Vertical;
  }
  else if (mouse_y > tolerance && mouse_y < (height - tolerance))
  {
    return ResizeType::Horizontal;
  }
  // Top left
  else if ((mouse_x <= tolerance && mouse_y <= tolerance))
  {
    return ResizeType::SouthWest;
  }
  // Bot right
  else if (mouse_x >= (width - tolerance)
      && mouse_y >= (height - tolerance))
  {
    return ResizeType::SouthWest;
  }
  // Bot left
  else if (mouse_x <= tolerance && mouse_y >= (height - tolerance))
  {
    return ResizeType::NorthEast;
  }
  // Should only activate for top right
  else
  {
    return ResizeType::NorthEast;
  }
}

void Window::resize_or_move(const QPointF& p)
{
  Qt::Edges edges;
  if (p.x() > width() - tolerance)
  {
    edges |= Qt::RightEdge;
  }
  if (p.x() < tolerance)
  {
    edges |= Qt::LeftEdge;
  }
  if (p.y() < tolerance)
  {
    edges |= Qt::TopEdge;
  }
  if (p.y() > height() - tolerance)
  {
    edges |= Qt::BottomEdge;
  }
  QWindow* handle = windowHandle();
  if (edges != 0 && !isMaximized())
  {
    if (handle->startSystemResize(edges)) {
    }
    else
    {
      std::cout << "Resize didn't work\n";
    }
  }
  else
  {
    if (handle->startSystemMove()) {
    }
    else
    {
      std::cout << "Move didn't work\n";
    }
  }
  title_bar->update_maxicon();
}

void Window::mousePressEvent(QMouseEvent* event)
{
  if (is_frameless())
  {
    resize_or_move(event->localPos());
  }
  else
  {
    QMainWindow::mousePressEvent(event);
  }
}

void Window::mouseReleaseEvent(QMouseEvent* event)
{
  Q_UNUSED(event) // at least, for now
}

void Window::mouseMoveEvent(QMouseEvent* event)
{
  if (isMaximized())
  {
    // No resizing
    return;
  }
  if (is_frameless())
  {
    const ResizeType type = should_resize(rect(), tolerance, event);
    setCursor(Qt::CursorShape(type));
  }
  else
  {
    setCursor(Qt::ArrowCursor);
    QMainWindow::mouseMoveEvent(event);
  }
}

void Window::resizeEvent(QResizeEvent* event)
{
  emit resize_done(size());
  title_bar->update_maxicon();
  QMainWindow::resizeEvent(event);
}

void Window::moveEvent(QMoveEvent* event)
{
#ifdef Q_OS_WIN
  static QScreen* cur_screen = nullptr;
  if (!is_frameless()) return;
  if (!cur_screen) cur_screen = screen();
  else if (cur_screen != screen())
  {
    cur_screen = screen();
    SetWindowPos((HWND) winId(), nullptr, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                   SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
    return;
  }
#endif
  title_bar->update_maxicon();
  QMainWindow::moveEvent(event);
}

void Window::listen_for_notification(
  std::string method,
  std::function<void (const ObjectArray&)> cb
)
{
  // Blocking std::function wrapper around an std::function
  // that sends a message to the main thread to call the std::function
  // that calls the callback std::function.
  nvim->set_notification_handler(
    std::move(method),
    [this, cb = std::move(cb)](Object obj) {
      QMetaObject::invokeMethod(
        this,
        [o = std::move(obj), cb = std::move(cb)]() {
          auto* arr = o.array();
          if (!arr || arr->size() < 3) return;
          auto* params = arr->at(2).array();
          if (!params) return;
          cb(std::move(*params));
        },
        Qt::QueuedConnection
      );
    }
  );
}

template<typename Res, typename Err>
void Window::handle_request(
  std::string req_name,
  handler_func<Res, Err> handler
)
{
  nvim->set_request_handler(
    std::move(req_name),
    [this, cb = std::move(handler)](Object obj) {
      QMetaObject::invokeMethod(
        this,
        [this, o = std::move(obj), f = std::move(cb)] {
          auto* arr = o.array();
          if (!arr || arr->size() < 4) return;
          auto msgid = arr->at(1).u64();
          auto params = arr->at(3).array();
          if (!msgid || !params) return;
          std::optional<Res> res;
          std::optional<Err> err;
          std::tie(res, err) = f(*arr);
          if (res)
          {
            nvim->send_response(*msgid, *res, msgpack::object());
          }
          else
          {
            nvim->send_response(*msgid, msgpack::object(), *err);
          }
        },
        Qt::QueuedConnection
      );
    }
  );
}

void Window::disable_frameless_window()
{
  auto flags = windowFlags();
  if (!(flags & Qt::FramelessWindowHint)) /* Already disabled */ return;
  if (title_bar) title_bar->hide();
  flags &= ~Qt::FramelessWindowHint;
#ifdef Q_OS_WIN
  remove_margin((HWND) winId());
#endif
  setWindowFlags(flags);
  showNormal();
  emit resize_done(size());
}

void Window::enable_frameless_window()
{
  auto flags = windowFlags();
  if (flags & Qt::FramelessWindowHint) /* Already enabled */ return;
  if (title_bar) title_bar->show();
  flags |= Qt::FramelessWindowHint;
  setWindowFlags(flags);
#ifdef Q_OS_WIN
  windows_setup_frameless((HWND) winId());
#endif
  // Kick the window out of any maximized/fullscreen state.
  showNormal();
  emit resize_done(size());
}

void Window::set_fullscreen(bool enable_fullscreen)
{
  if (isFullScreen() == enable_fullscreen) return;
  if (enable_fullscreen) fullscreen();
  else
  {
    un_fullscreen();
  }
}

void Window::changeEvent(QEvent* event)
{
  if (event->type() == QEvent::WindowStateChange)
  {
    emit win_state_changed(windowState());
    auto ev = static_cast<QWindowStateChangeEvent*>(event);
    prev_state = ev->oldState();
  }
#ifdef Q_OS_WIN
    if ((windowState() & Qt::WindowMaximized) && is_frameless())
    {
      // 8px bigger on each side when maximized on Windows as a frameless
      // window
      setContentsMargins(8, 8, 8, 8);
    }
    else
    {
      setContentsMargins(0, 0, 0, 0);
    }
#endif
  QMainWindow::changeEvent(event);
}

bool Window::nativeEvent(const QByteArray& e_type, void* msg, long* result)
{
  Q_UNUSED(e_type);
#ifdef Q_OS_WIN
  if (!is_frameless()) return false;
  MSG* message = static_cast<MSG*>(msg);
  switch(message->message)
  {
    case WM_NCCALCSIZE:
      *result = 0;
      return true;
  }
#else
  Q_UNUSED(msg);
  Q_UNUSED(result);
#endif
  return false;
}

void Window::maximize()
{
  setWindowState(Qt::WindowMaximized);
  setGeometry(screen()->availableGeometry());
}

void Window::fullscreen()
{
  if (isFullScreen()) return;
  title_bar->hide();
#ifdef Q_OS_WIN
  if (is_frameless())
  {
    // Remove margins (otherwise you see a border in fullscreen mode)
    remove_margin((HWND) winId());
  }
#endif
  showFullScreen();
  emit resize_done(size());
}

void Window::un_fullscreen()
{
  if (!isFullScreen()) return;
#ifdef Q_OS_WIN
  if (is_frameless())
  {
    // Set the margins back to get the aero effects
    add_margin((HWND) winId(), {0, 0, 1, 0});
  }
#endif
  if (prev_state & Qt::WindowMaximized) maximize();
  else showNormal();
  show_title_bar();
  emit resize_done(size());
}

void Window::update_titlebar_colors()
{
  const HLAttr& def_clrs = hl_state.default_colors_get();
  auto fg = titlebar_colors.first.value_or(def_clrs.fg()->qcolor());
  auto bg = titlebar_colors.second.value_or(def_clrs.bg()->qcolor());
  title_bar->set_color(fg, bg);
}

void Window::closeEvent(QCloseEvent* event)
{
  event->ignore();
  if (nvim->exited())
  {
    QWidget::closeEvent(event);
  }
  else
  {
    nvim->command("confirm qa");
  }
}
