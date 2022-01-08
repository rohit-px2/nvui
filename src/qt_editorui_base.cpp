#include "qt_editorui_base.hpp"
#include "input.hpp"
#include "nvim_utils.hpp"
#include "scalers.hpp"
#include <QApplication>
#include <QDir>
#include <QMimeData>
#include <fmt/format.h>

QtEditorUIBase::QtEditorUIBase(
  QWidget& inheritor_instance,
  int cols,
  int rows,
  std::unordered_map<std::string, bool> capabilities,
  std::string nvim_path,
  std::vector<std::string> nvim_args
)
: EditorBase(std::move(nvim_path), std::move(nvim_args), &inheritor_instance),
  inheritor(inheritor_instance),
  ui_attach_info {cols, rows, std::move(capabilities)},
  cwd()
{
  idle_timer.setSingleShot(true);
  idle_timer.setInterval(100000);
  idle_timer.callOnTimeout([&] { idle(); });
  idle_timer.start();
  idle_timer.setTimerType(Qt::VeryCoarseTimer);
}

void QtEditorUIBase::setup()
{
  Base::setup();
  register_command_handlers();
  QObject::connect(&n_cursor, &Cursor::anim_state_changed, &inheritor, [this] {
    inheritor.update();
  });
  QObject::connect(&n_cursor, &Cursor::cursor_hidden, &inheritor, [this] {
    inheritor.update();
  });
  QObject::connect(&n_cursor, &Cursor::cursor_visible, &inheritor, [this] {
    inheritor.update();
  });
}

void QtEditorUIBase::attach()
{
  const auto& [cols, rows, capabilities] = ui_attach_info;
  nvim_ui_attach(cols, rows, capabilities);
}

void QtEditorUIBase::set_scaler(scalers::time_scaler& sc, const std::string& name)
{
  const auto& scalers = scalers::scalers();
  if (!scalers.contains(name)) return;
  sc = scalers.at(name);
}

static std::string mouse_button_to_string(Qt::MouseButtons btn)
{
  switch(btn)
  {
    case Qt::LeftButton:
      return std::string("left");
    case Qt::RightButton:
      return std::string("right");
    case Qt::MiddleButton:
      return std::string("middle");
    default:
      return std::string("");
  }
}

static std::string mouse_mods_to_string(
  Qt::KeyboardModifiers mods,
  int click_count = 0
)
{
  std::string mod_str;
  if (mods & Qt::ControlModifier) mod_str.push_back('c');
  if (mods & Qt::AltModifier) mod_str.push_back('a');
  if (mods & Qt::ShiftModifier) mod_str.push_back('s');
  if (click_count > 1) mod_str.append(std::to_string(click_count));
  return mod_str;
}

/// Returns a point clamped between the top-left and bottom-right
/// of r.
static QPoint clamped(QPoint p, const QRect& r)
{
  p.setX(std::clamp(p.x(), r.left(), r.right()));
  p.setY(std::clamp(p.y(), r.top(), r.bottom()));
  return p;
}

static QRect scaled(const QRect& r, float hor, float vert)
{
  return QRectF(
    r.x() * hor,
    r.y() * vert,
    r.width() * hor,
    r.height() * vert
  ).toRect();
}

void QtEditorUIBase::handle_key_press(QKeyEvent* event)
{
  un_idle();
  typed();
  event->accept();
  auto text = convert_key(*event);
  if (text.empty()) return;
  nvim->send_input(std::move(text));
}

QVariant QtEditorUIBase::handle_ime_query(Qt::InputMethodQuery query)
{
  switch(query)
  {
    case Qt::ImFont:
    {
      QFont font;
      font.setFamily(default_font_family());
      if (!guifonts.empty())
      {
        const auto& desc = guifonts.front();
        font.setFamily(QString::fromStdString(desc.name));
        font.setPointSizeF(desc.point_size);
        font::set_opts(font, desc.base_options);
      }
      return font;
    }
    case Qt::ImCursorRectangle:
    {
      auto&& [font_width, font_height] = font_dimensions();
      auto rect_opt = n_cursor.rect(font_width, font_height, true);
      if (!rect_opt) return QVariant();
      auto cr = *rect_opt;
      return cr.rect;
    }
    default:
      break;
  }
  return QVariant();
}

void QtEditorUIBase::handle_ime_event(QInputMethodEvent* event)
{
  event->accept();
  QString commit_string = event->commitString();
  if (!commit_string.isEmpty())
  {
    nvim->send_input(commit_string.toStdString());
  }
}

void QtEditorUIBase::handle_nvim_resize(QResizeEvent* ev)
{
  ev->accept();
  screen_resized(ev->size().width(), ev->size().height());
}

std::optional<QtEditorUIBase::GridPos>
QtEditorUIBase::grid_pos_for(QPoint pos) const
{
  const auto [f_width, f_height] = font_dimensions();
  for(auto it = grids.rbegin(); it != grids.rend(); ++it)
  {
    const auto& grid = *it;
    QRect grid_rect = {grid->x, grid->y, grid->cols, grid->rows};
    QRect grid_px_rect = scaled(grid_rect, f_width, f_height);
    if (grid_px_rect.contains(pos))
    {
      int row = (pos.y() / f_height) - grid->y;
      int col = (pos.x() / f_width) - grid->x;
      auto x = clamped({col, row}, {0, 0, grid->cols, grid->rows});
      return GridPos {grid->id, x.y(), x.x()};
    }
  }
  return std::nullopt;
}

void QtEditorUIBase::send_mouse_input(
  QPoint pos,
  std::string btn,
  std::string action,
  std::string mods
)
{
  if (!mouse_enabled()) return;
  auto grid_pos_opt = grid_pos_for(pos);
  if (!grid_pos_opt)
  {
    return;
  }
  auto&& [grid_num, row, col] = *grid_pos_opt;
  if (!ext.multigrid) grid_num = 0;
  if (action == "press")
  {
    mouse.gridid = grid_num;
    mouse.row = row;
    mouse.col = col;
  }
  nvim->input_mouse(
    std::move(btn), std::move(action), std::move(mods),
    grid_num, row, col
  );
}

void QtEditorUIBase::handle_mouse_press(QMouseEvent* event)
{
  un_idle();
  unhide_cursor();
  if (!mouse_enabled()) return;
  mouse.button_clicked(event->button());
  std::string btn_text = mouse_button_to_string(event->button());
  if (btn_text.empty()) return;
  std::string action = "press";
  std::string mods = mouse_mods_to_string(event->modifiers(), mouse.click_count);
  send_mouse_input(
    event->pos(), std::move(btn_text), std::move(action), std::move(mods)
  );
}

void QtEditorUIBase::handle_wheel(QWheelEvent* event)
{
  un_idle();
  unhide_cursor();
  if (!mouse_enabled()) return;
  std::string button = "wheel";
  std::string action = "";
  auto mods = mouse_mods_to_string(event->modifiers());
  if (event->angleDelta().y() > 0) /* scroll up */ action = "up";
  else if (event->angleDelta().y() < 0) /* scroll down */ action = "down";
  if (action.empty()) return;
  QPoint wheel_pos = event->position().toPoint();
  send_mouse_input(
    wheel_pos, std::move(button), std::move(action), std::move(mods)
  );
}

void QtEditorUIBase::handle_mouse_move(QMouseEvent* event)
{
  un_idle();
  unhide_cursor();
  if (!mouse_enabled()) return;
  auto button = mouse_button_to_string(event->buttons());
  if (button.empty()) return;
  auto mods = mouse_mods_to_string(event->modifiers());
  std::string action = "drag";
  const auto [f_width, f_height] = font_dimensions();
  QPoint text_pos(event->x() / f_width, event->y() / f_height);
  int grid_num = 0;
  if (mouse.gridid)
  {
    auto* grid = find_grid(mouse.gridid);
    if (grid)
    {
      text_pos.rx() -= grid->x;
      text_pos.ry() -= grid->y;
      text_pos = clamped(text_pos, {0, 0, grid->cols, grid->rows});
    }
    grid_num = mouse.gridid;
  }
  // Check if mouse actually moved
  if (mouse.col == text_pos.x() && mouse.row == text_pos.y()) return;
  mouse.col = text_pos.x();
  mouse.row = text_pos.y();
  nvim->input_mouse(
    button, action, mods, grid_num, mouse.row, mouse.col
  );
}

void QtEditorUIBase::handle_mouse_release(QMouseEvent* event)
{
  un_idle();
  unhide_cursor();
  if (!mouse_enabled()) return;
  auto button = mouse_button_to_string(event->button());
  if (button.empty()) return;
  auto mods = mouse_mods_to_string(event->modifiers());
  std::string action = "release";
  mouse.gridid = 0;
  send_mouse_input(
    event->pos(), std::move(button), std::move(action), std::move(mods)
  );
}

static bool is_image(const QString& fp)
{
  return !QImage(fp).isNull();
}

void QtEditorUIBase::handle_drop(QDropEvent* event)
{
  unhide_cursor();
  const QMimeData* mime_data = event->mimeData();
  if (mime_data->hasUrls())
  {
    for(const auto& url : mime_data->urls())
    {
      if (!url.isLocalFile()) continue;
      if (is_image(url.toLocalFile()))
      {
        // Do something special for images?
      }
      std::string cmd = fmt::format("e {}", url.toLocalFile().toStdString());
      nvim->command(cmd);
    }
  }
  event->acceptProposedAction();
}

void QtEditorUIBase::handle_drag(QDragEnterEvent* event)
{
  unhide_cursor();
  if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void QtEditorUIBase::handle_focuslost(QFocusEvent*)
{
  nvim->command("if exists('#FocusLost') | doautocmd <nomodeline> FocusLost | endif");
}
void QtEditorUIBase::handle_focusgained(QFocusEvent*)
{
  nvim->command("if exists('#FocusGained') | doautocmd <nomodeline> FocusGained | endif");
}

void QtEditorUIBase::set_animations_enabled(bool enabled)
{
  animate = enabled;
}

float QtEditorUIBase::charspacing() const
{
  return charspace;
}

float QtEditorUIBase::linespacing() const
{
  return linespace;
}

float QtEditorUIBase::move_animation_duration() const
{
  return move_animation.duration;
}

int QtEditorUIBase::move_animation_frametime() const
{
  return move_animation.ms_interval;
}

float QtEditorUIBase::scroll_animation_duration() const
{
  return scroll_animation.duration;
}

int QtEditorUIBase::scroll_animation_frametime() const
{
  return scroll_animation.ms_interval;
}

float QtEditorUIBase::cursor_animation_duration() const
{
  return cursor_animation.duration;
}

int QtEditorUIBase::cursor_animation_frametime() const
{
  return cursor_animation.ms_interval;
}

bool QtEditorUIBase::animations_enabled() const
{
  return animate;
}

void QtEditorUIBase::idle()
{
  if (!should_idle) return;
  idle_state = IdleState {animate};
  set_animations_enabled(false);
}

void QtEditorUIBase::un_idle()
{
  if (!idling()) return;
  set_animations_enabled(idle_state.value().were_animations_enabled);
  idle_state.reset();
  idle_timer.start();
}

void QtEditorUIBase::register_command_handlers()
{
  const auto on = [&](auto&&... args) { listen_for_notification(args...); };
  on("NVUI_WINOPACITY", paramify<float>([this](double opacity) {
    if (opacity <= 0.0 || opacity > 1.0) return;
    emit signaller.window_opacity_changed(opacity);
  }));
  on("NVUI_TOGGLE_FRAMELESS", [this](const auto&) {
    emit signaller.titlebar_toggled();
  });
  on("NVUI_CHARSPACE", paramify<float>([this](float space) {
    charspace = space;
    charspace_changed(charspace);
  }));
  on("NVUI_PUM_BORDER_WIDTH", paramify<std::size_t>([this](std::size_t b_width) {
    popup_menu->set_border_width(b_width);
  }));
  on("NVUI_PUM_BORDER_COLOR", paramify<QString>([this](QString potential_clr) {
    if (!QColor::isValidColor(potential_clr)) return;
    QColor new_pum_border_color {potential_clr};
    popup_menu->set_border_color(Color(new_pum_border_color.rgb()));
  }));
  on("NVUI_CMD_FONT_SIZE", paramify<float>([this](double size) {
    if (size <= 0.f) return;
    cmdline->set_font_size(size);
  }));
  on("NVUI_CMD_FONT_FAMILY", paramify<std::string>([this](std::string family) {
    cmdline->set_font_family(family);
  }));
  on("NVUI_CMD_BG", paramify<QString>([this](QString bg_str) {
    if (!QColor::isValidColor(bg_str)) return;
    QColor bg {bg_str};
    cmdline->set_bg(bg.rgb());
  }));
  on("NVUI_CMD_FG", paramify<QString>([this](QString fg_str) {
    if (!QColor::isValidColor(fg_str)) return;
    QColor fg {fg_str};
    cmdline->set_fg(fg.rgb());
  }));
  on("NVUI_CMD_BORDER_WIDTH", paramify<int>([this](int width) {
    if (width < 0.f) return;
    cmdline->set_border_width(width);
  }));
  on("NVUI_CMD_BORDER_COLOR", paramify<QString>([this](QString color_str) {
    if (!QColor::isValidColor(color_str)) return;
    QColor color {color_str};
    cmdline->set_border_color(color.rgb());
  }));
  on("NVUI_CMD_SET_LEFT", paramify<float>([this](float new_x) {
    if (new_x < 0.f || new_x > 1.f) return;
    cmdline->set_x(new_x);
  }));
  on("NVUI_CMD_YPOS", paramify<float>([this](float new_y) {
    if (new_y < 0.f || new_y > 1.f) return;
    cmdline->set_y(new_y);
  }));
  on("NVUI_CMD_WIDTH", paramify<float>([this](float new_width) {
    if (new_width < 0.f || new_width > 1.f) return;
    cmdline->set_width(new_width);
  }));
  on("NVUI_CMD_HEIGHT", paramify<float>([this](float new_height) {
    if (new_height < 0.f || new_height > 1.f) return;
    cmdline->set_height(new_height);
  }));
  on("NVUI_CMD_SET_CENTER_X", paramify<float>([this](float center_x) {
    if (center_x < 0.f || center_x > 1.f) return;
    cmdline->set_center_x(center_x);
  }));
  on("NVUI_CMD_SET_CENTER_Y", paramify<float>([this](float center_y) {
    if (center_y < 0.f || center_y > 1.f) return;
    cmdline->set_center_y(center_y);
  }));
  on("NVUI_CMD_PADDING", paramify<int>([this](int padding) {
    cmdline->set_padding(padding);
  }));
  on("NVUI_FULLSCREEN", paramify<bool>([this](bool b) {
    emit signaller.fullscreen_set(b);
  }));
  on("NVUI_TOGGLE_FULLSCREEN", [this](auto) {
    emit signaller.fullscreen_toggled();
  });
  on("NVUI_TITLEBAR_FONT_FAMILY", paramify<QString>([this](QString family) {
    emit signaller.titlebar_font_family_set(family);
  }));
  on("NVUI_TITLEBAR_FONT_SIZE", paramify<double>([&](double sz) {
    emit signaller.titlebar_font_size_set(sz);
  }));
  on("NVUI_TITLEBAR_COLORS",
    paramify<QString, QString>([this](QString fg, QString bg) {
      QColor fgc = fg, bgc = bg;
      if (!fgc.isValid() || !bgc.isValid()) return;
      emit signaller.titlebar_fg_bg_set(fgc, bgc);
  }));
  on("NVUI_TITLEBAR_UNSET_COLORS", [this](auto) {
    emit signaller.titlebar_colors_unset();
  });
  on("NVUI_TITLEBAR_FG", paramify<QString>([this](QString fgs) {
    QColor fg = fgs;
    if (!fg.isValid()) return;
    emit signaller.titlebar_fg_set(fg);
  }));
  on("NVUI_TITLEBAR_BG", paramify<QString>([this](QString bgs) {
    QColor bg = bgs;
    if (!bg.isValid()) return;
    emit signaller.titlebar_bg_set(bg);
  }));
  on("NVUI_MOVE_ANIMATION_FRAMETIME", paramify<int>([this](int ms) {
    move_animation.set_interval(ms);
  }));
  on("NVUI_ANIMATIONS_ENABLED", paramify<bool>([this](bool enabled) {
    set_animations_enabled(enabled);
  }));
  on("NVUI_MOVE_ANIMATION_DURATION", paramify<float>([this](float s) {
    move_animation.set_duration(s);
  }));
  on("NVUI_SCROLL_ANIMATION_DURATION", paramify<float>([this](float dur) {
    scroll_animation.set_duration(dur);
  }));
  on("NVUI_SNAPSHOT_LIMIT", paramify<u32>([this](u32 limit) {
    snapshot_count = limit;
  }));
  on("NVUI_SCROLL_FRAMETIME", paramify<int>([this](int ms) {
    scroll_animation.set_interval(ms);
  }));
  on("NVUI_SCROLL_SCALER",
      paramify<std::string>([this](std::string s) {
        set_scaler(GridBase::scroll_scaler, s);
    }
  ));
  on("NVUI_MOVE_SCALER",
      paramify<std::string>([this](std::string s) {
        set_scaler(GridBase::move_scaler, s);
  }));
  on("NVUI_FRAMELESS", paramify<bool>([this](bool b) {
    emit signaller.titlebar_set(b);
  }));
  on("NVUI_CURSOR_HIDE_TYPE",
    paramify<bool>([this](bool hide) {
      mousehide = hide;
      inheritor.unsetCursor();
  }));
  on("NVUI_TB_TITLE",
    paramify<QString>([this](QString text) {
      emit signaller.title_changed(text);
  }));
  on("NVUI_IME_SET",
    paramify<bool>([this](bool enable) {
      inheritor.setAttribute(Qt::WA_InputMethodEnabled, enable);
  }));
  on("NVUI_IME_TOGGLE", [&](const auto&) {
    constexpr auto ime = Qt::WA_InputMethodEnabled;
    if (inheritor.testAttribute(ime))
    {
      inheritor.setAttribute(ime, false);
    }
    else inheritor.setAttribute(ime, true);
  });
  on("NVUI_EXT_POPUPMENU",
    paramify<bool>([this](bool b) {
      nvim->ui_set_option("ext_popupmenu", b);
  }));
  on("NVUI_EXT_CMDLINE",
    paramify<bool>([this](bool b) {
      nvim->ui_set_option("ext_cmdline", b);
  }));
  on("NVUI_IDLE_WAIT_FOR",
    paramify<double>([this](double seconds) {
      idle_timer.setInterval(seconds * 1000);
  }));
  on("NVUI_EDITOR_SPAWN", [this](const ObjectArray& params) {
    if (params.empty()) spawn_editor_with_params(Object::null);
    else spawn_editor_with_params(params.front());
  });
  on("NVUI_EDITOR_SWITCH", paramify<int>([this](int index) {
    if (index < 0)
    {
      nvim->err_write("Editor index cannot be negative.\n");
      return;
    }
    emit signaller.editor_switched(std::size_t(index));
  }));
  on("NVUI_DIR_CHANGED", paramify<std::string>([this](std::string dir) {
    cwd = std::move(dir);
    emit signaller.cwd_changed(current_dir());
  }));
  on("NVUI_EDITOR_PREV", [this](const auto&) {
    emit signaller.editor_changed_previous();
  });
  on("NVUI_EDITOR_NEXT", [this](const auto&) {
    emit signaller.editor_changed_next();
  });
  on("NVUI_EDITOR_SELECT", [this](const auto&) {
    emit signaller.editor_selection_list_opened();
  });
  using namespace std;
  handle_request<vector<string>, int>(*nvim, "NVUI_SCALER_NAMES",
    [&](const auto&) {
      return tuple {scalers::scaler_names(), std::nullopt};
  }, &inheritor);
  nvim->set_var("nvui_tb_separator", " • ");
  nvim->exec_viml(R"(
  function! NvuiGetChan()
    let chans = nvim_list_chans()
    for chan in chans
      if has_key(chan, 'name') && chan['name'] == 'nvui'
        return chan['id']
      endif
    endfor
    return 1
  endfunction
  let g:nvui_rpc_chan = NvuiGetChan()
  function! NvuiNotify(name, ...)
    call call("rpcnotify", extend([g:nvui_rpc_chan, a:name], a:000))
  endfunction
  function! NvuiCompletePopup(arg, line, pos)
    return rpcrequest(g:nvui_rpc_chan, 'NVUI_POPUPMENU_ICON_NAMES')
  endfunction
  function! NvuiComplete_scaler(arg, line, pos)
    return rpcrequest(g:nvui_rpc_chan, 'NVUI_SCALER_NAMES')
  endfunction
  function! NvuiGet_dir()
    return fnamemodify(getcwd(), ':t')
  endfunction
  function! NvuiGet_file()
    return expand('%:t')
  endfunction
  function! NvuiComplete_cursoreff(arg, line, pos)
    return rpcrequest(g:nvui_rpc_chan, 'NVUI_CURSOR_EFFECT_NAMES')
  endfunction
  function! NvuiGet_title()
    return join(split("nvui," . NvuiGet_dir() . "," . NvuiGet_file(), ","), g:nvui_tb_separator)
  endfunction
  function! NvuiComplete_ceff_scaler(arg, line, pos)
    return rpcrequest(g:nvui_rpc_chan, 'NVUI_CURSOR_EFFECT_SCALERS')
  endfunction
  command! -nargs=1 -complete=customlist,NvuiComplete_ceff_scaler NvuiCursorEffectScaler call NvuiNotify("NVUI_CURSOR_EFFECT_SCALER", <f-args>)
  command! -nargs=1 -complete=customlist,NvuiComplete_cursoreff NvuiCursorEffect call NvuiNotify('NVUI_CURSOR_EFFECT', <f-args>)
  command! -nargs=1 NvuiCursorEffectFrametime call rpcnotify(g:nvui_rpc_chan, 'NVUI_CURSOR_EFFECT_FRAMETIME', <args>)
  command! -nargs=1 NvuiCursorEffectDuration call rpcnotify(g:nvui_rpc_chan, 'NVUI_CURSOR_EFFECT_DURATION', <args>)
  command! -nargs=1 NvuiPopupMenuInfoColumns call rpcnotify(g:nvui_rpc_chan, 'NVUI_PUM_INFO_COLS', <args>)
  command! -nargs=1 NvuiScrollAnimationDuration call rpcnotify(g:nvui_rpc_chan, 'NVUI_SCROLL_ANIMATION_DURATION', <args>)
  command! -nargs=1 NvuiSnapshotLimit call rpcnotify(g:nvui_rpc_chan, 'NVUI_SNAPSHOT_LIMIT', <args>)
  command! -nargs=1 NvuiScrollFrametime call rpcnotify(g:nvui_rpc_chan, 'NVUI_SCROLL_FRAMETIME', <args>)
  command! -nargs=1 -complete=customlist,NvuiComplete_scaler NvuiScrollScaler call NvuiNotify('NVUI_SCROLL_SCALER', <f-args>)
  command! -nargs=1 -complete=customlist,NvuiComplete_scaler NvuiMoveScaler call NvuiNotify('NVUI_MOVE_SCALER', <f-args>)
  command! -nargs=1 -complete=customlist,NvuiComplete_scaler NvuiCursorScaler call NvuiNotify('NVUI_CURSOR_SCALER', <f-args>)
  command! -nargs=1 NvuiMoveAnimationFrametime call rpcnotify(g:nvui_rpc_chan, 'NVUI_MOVE_ANIMATION_FRAMETIME', <args>)
  command! -nargs=1 NvuiAnimationsEnabled call rpcnotify(g:nvui_rpc_chan, 'NVUI_ANIMATIONS_ENABLED', <args>)
  command! -nargs=1 NvuiMoveAnimationDuration call rpcnotify(g:nvui_rpc_chan, 'NVUI_MOVE_ANIMATION_DURATION', <args>)
  command! -nargs=1 NvuiTitlebarBg call NvuiNotify('NVUI_TITLEBAR_BG', <f-args>)
  command! -nargs=1 NvuiTitlebarFg call NvuiNotify('NVUI_TITLEBAR_FG', <f-args>)
  command! -nargs=* NvuiTitlebarColors call NvuiNotify('NVUI_TITLEBAR_COLORS', <f-args>)
  command! NvuiTitlebarUnsetColors call NvuiNotify('NVUI_TITLEBAR_UNSET_COLORS')
  command! -nargs=1 NvuiTitlebarFontFamily call NvuiNotify('NVUI_TITLEBAR_FONT_FAMILY', <f-args>)
  command! -nargs=1 NvuiTitlebarFontSize call rpcnotify(g:nvui_rpc_chan, 'NVUI_TITLEBAR_FONT_SIZE', <args>)
  command! -nargs=1 NvuiCaretExtendTop call rpcnotify(g:nvui_rpc_chan, 'NVUI_CARET_EXTEND_TOP', <args>)
  command! -nargs=1 NvuiCaretExtendBottom call rpcnotify(g:nvui_rpc_chan, 'NVUI_CARET_EXTEND_BOTTOM', <args>)
  command! -nargs=1 NvuiTitlebarSeparator call rpcnotify(g:nvui_rpc_chan, 'NVUI_TB_SEPARATOR', <args>)
  command! -nargs=* -complete=customlist,NvuiCompletePopup NvuiPopupMenuIconFgBg call NvuiNotify('NVUI_PUM_ICON_COLORS', <f-args>)
  command! -nargs=* -complete=customlist,NvuiCompletePopup NvuiPopupMenuIconBg call NvuiNotify('NVUI_PUM_ICON_BG', <f-args>)
  command! -nargs=* -complete=customlist,NvuiCompletePopup NvuiPopupMenuIconFg call NvuiNotify('NVUI_PUM_ICON_FG', <f-args>)
  command! -nargs=1 NvuiPopupMenuIconsRightAlign call rpcnotify(g:nvui_rpc_chan, 'NVUI_PUM_ICONS_RIGHT', <args>)
  command! -nargs=1 NvuiCmdPadding call rpcnotify(g:nvui_rpc_chan, 'NVUI_CMD_PADDING', <args>)
  command! -nargs=1 NvuiCmdCenterXPos call rpcnotify(g:nvui_rpc_chan, 'NVUI_CMD_SET_CENTER_X', <args>)
  command! -nargs=1 NvuiCmdCenterYPos call rpcnotify(g:nvui_rpc_chan, 'NVUI_CMD_SET_CENTER_Y', <args>)
  command! -nargs=1 NvuiCmdLeftPos call rpcnotify(g:nvui_rpc_chan, 'NVUI_CMD_SET_LEFT', <args>)
  command! -nargs=1 NvuiCmdTopPos call rpcnotify(g:nvui_rpc_chan, 'NVUI_CMD_YPOS', <args>)
  command! -nargs=1 NvuiCmdWidth call rpcnotify(g:nvui_rpc_chan, 'NVUI_CMD_WIDTH', <args>)
  command! -nargs=1 NvuiCmdHeight call rpcnotify(g:nvui_rpc_chan, 'NVUI_CMD_HEIGHT', <args>)
  command! -nargs=1 NvuiCmdFontSize call rpcnotify(g:nvui_rpc_chan, 'NVUI_CMD_FONT_SIZE', <args>)
  command! -nargs=1 NvuiCmdFontFamily call NvuiNotify('NVUI_CMD_FONT_FAMILY', <f-args>)
  command! -nargs=1 NvuiCmdFg call NvuiNotify('NVUI_CMD_FG', <f-args>)
  command! -nargs=1 NvuiCmdBg call NvuiNotify('NVUI_CMD_BG', <f-args>)
  command! -nargs=1 NvuiCmdBorderWidth call rpcnotify(g:nvui_rpc_chan, 'NVUI_CMD_BORDER_WIDTH', <args>)
  command! -nargs=1 NvuiCmdBorderColor call NvuiNotify('NVUI_CMD_BORDER_COLOR', <f-args>)
  command! -nargs=1 NvuiCmdBigFontScaleFactor call rpcnotify(g:nvui_rpc_chan, 'NVUI_CMD_BIG_SCALE', <args>)
  command! -nargs=1 NvuiPopupMenuDefaultIconFg call NvuiNotify('NVUI_PUM_DEFAULT_ICON_FG', <f-args>)
  command! -nargs=1 NvuiPopupMenuDefaultIconBg call NvuiNotify('NVUI_PUM_DEFAULT_ICON_BG', <f-args>)
  command! -nargs=1 NvuiPopupMenuIconSpacing call rpcnotify(g:nvui_rpc_chan, 'NVUI_PUM_ICON_SPACING', <args>)
  command! NvuiPopupMenuIconsToggle call rpcnotify(g:nvui_rpc_chan, 'NVUI_PUM_ICONS_TOGGLE')
  command! -nargs=1 NvuiPopupMenuIconOffset call rpcnotify(g:nvui_rpc_chan, 'NVUI_PUM_ICON_OFFSET', <args>)
  command! -nargs=1 NvuiPopupMenuBorderColor call NvuiNotify('NVUI_PUM_BORDER_COLOR', <f-args>)
  command! -nargs=1 NvuiPopupMenuBorderWidth call rpcnotify(g:nvui_rpc_chan, 'NVUI_PUM_BORDER_WIDTH', <args>)
  command! -nargs=1 NvuiPopupMenuMaxChars call rpcnotify(g:nvui_rpc_chan, 'NVUI_PUM_MAX_CHARS', <args>)
  command! -nargs=1 NvuiPopupMenuMaxItems call rpcnotify(g:nvui_rpc_chan, 'NVUI_PUM_MAX_ITEMS', <args>)
  command! NvuiToggleFrameless call rpcnotify(g:nvui_rpc_chan, 'NVUI_TOGGLE_FRAMELESS')
  command! -nargs=1 NvuiOpacity call rpcnotify(g:nvui_rpc_chan, 'NVUI_WINOPACITY', <args>)
  command! -nargs=1 NvuiCharspace call rpcnotify(g:nvui_rpc_chan, 'NVUI_CHARSPACE', <args>)
  command! -nargs=1 NvuiFullscreen call rpcnotify(g:nvui_rpc_chan, 'NVUI_FULLSCREEN', <args>)
  command! NvuiToggleFullscreen call rpcnotify(g:nvui_rpc_chan, 'NVUI_TOGGLE_FULLSCREEN')
  command! -nargs=1 NvuiFrameless call rpcnotify(g:nvui_rpc_chan, 'NVUI_FRAMELESS', <args>)
  command! -nargs=1 NvuiCursorAnimationDuration call rpcnotify(g:nvui_rpc_chan, 'NVUI_CURSOR_ANIMATION_DURATION', <args>)
  command! -nargs=1 NvuiCursorFrametime call rpcnotify(g:nvui_rpc_chan, 'NVUI_CURSOR_FRAMETIME', <args>)
  command! -nargs=1 NvuiCursorHideWhileTyping call rpcnotify(g:nvui_rpc_chan, 'NVUI_CURSOR_HIDE_TYPE', <args>)
  command! -nargs=1 NvuiPopupMenu call rpcnotify(g:nvui_rpc_chan, 'NVUI_EXT_POPUPMENU', <args>)
  command! -nargs=1 NvuiCmdline call rpcnotify(g:nvui_rpc_chan, 'NVUI_EXT_CMDLINE', <args>)
  command! -nargs=1 NvuiSecondsBeforeIdle call rpcnotify(g:nvui_rpc_chan, 'NVUI_IDLE_WAIT_FOR', <args>)
  command! NvuiIMEEnable call rpcnotify(g:nvui_rpc_chan, 'NVUI_IME_SET', v:true)
  command! NvuiIMEDisable call rpcnotify(g:nvui_rpc_chan, 'NVUI_IME_SET', v:false)
  command! NvuiIMEToggle call rpcnotify(g:nvui_rpc_chan, 'NVUI_IME_TOGGLE')
  command! -nargs=? NvuiEditorSpawn call rpcnotify(g:nvui_rpc_chan, 'NVUI_EDITOR_SPAWN', <args>)
  command! -nargs=1 NvuiEditorSwitch call rpcnotify(g:nvui_rpc_chan, 'NVUI_EDITOR_SWITCH', <args>)
  command! NvuiEditorPrev call rpcnotify(g:nvui_rpc_chan, 'NVUI_EDITOR_PREV')
  command! NvuiEditorNext call rpcnotify(g:nvui_rpc_chan, 'NVUI_EDITOR_NEXT')
  command! NvuiEditorSelect call rpcnotify(g:nvui_rpc_chan, 'NVUI_EDITOR_SELECT')
  function! NvuiGetTitle()
    return NvuiGet_title()
  endfunction
  augroup nvui_autocmds
    autocmd!
    autocmd BufEnter * call rpcnotify(g:nvui_rpc_chan, 'NVUI_TB_TITLE', NvuiGetTitle())
    autocmd DirChanged * call rpcnotify(g:nvui_rpc_chan, 'NVUI_TB_TITLE', NvuiGetTitle())
    autocmd DirChanged * call rpcnotify(g:nvui_rpc_chan, 'NVUI_DIR_CHANGED', getcwd())
  augroup END
  call rpcnotify(g:nvui_rpc_chan, 'NVUI_DIR_CHANGED', getcwd())
  )");
  auto script_dir = constants::script_dir().toStdString();
  nvim->command(fmt::format("helptags {}", script_dir + "/doc"));
  nvim->command(fmt::format("set rtp+={}", script_dir));
}

void QtEditorUIBase::listen_for_notification(
  std::string method,
  std::function<void (const ObjectArray&)> cb
)
{
  ::listen_for_notification(
    *nvim, std::move(method), std::move(cb), &inheritor
  );
}

UISignaller* QtEditorUIBase::ui_signaller() { return &signaller; }

bool QtEditorUIBase::idling() const { return idle_state.has_value(); }

u32 QtEditorUIBase::snapshot_limit() const { return snapshot_count; }

void QtEditorUIBase::do_close()
{
  emit signaller.closed();
}

void QtEditorUIBase::default_colors_changed(Color fg, Color bg)
{
  send_redraw();
  emit signaller.default_colors_changed(fg.qcolor(), bg.qcolor());
}

void QtEditorUIBase::field_updated(std::string_view field, const Object& val)
{
  if (field == "linespace")
  {
    linespace = val.try_convert<float>().value_or(linespace);
    linespace_changed(linespace);
  }
}

void QtEditorUIBase::typed()
{
  if (!mousehide || inheritor.cursor() == Qt::BlankCursor) return;
  inheritor.setCursor(Qt::BlankCursor);
}

void QtEditorUIBase::unhide_cursor()
{
  if (inheritor.cursor() == Qt::BlankCursor) inheritor.unsetCursor();
}

void QtEditorUIBase::cursor_moved()
{
  qApp->inputMethod()->update(Qt::ImCursorRectangle);
}

void QtEditorUIBase::spawn_editor_with_params(const Object& params)
{
  auto nvim_path = params.try_at("nvim").try_convert<std::string>().value_or(path_to_nvim);
  auto multigrid = params.try_at("multigrid").try_convert<bool>().value_or(ext.multigrid);
  auto popup = params.try_at("popupmenu").try_convert<bool>().value_or(ext.popupmenu);
  auto cmdline = params.try_at("cmdline").try_convert<bool>().value_or(ext.cmdline);
  std::unordered_map<std::string, bool> capabilities = {
    {"ext_multigrid", multigrid},
    {"ext_popupmenu", popup},
    {"ext_cmdline", cmdline},
    {"ext_linegrid", true},
    {"ext_hlstate", false},
    {"ext_tabline", false},
  };
  std::vector<std::string> args = Nvim::default_args();
  bool all_strings = true;
  if (auto arglist = params.try_at("nvim_args").array())
  {
    for(const auto& arg : *arglist)
    {
      if (!arg.is_string())
      {
        all_strings = false;
        break;
      }
      args.push_back(arg.get<std::string>());
    }
    if (!all_strings)
    {
      args = Nvim::default_args();
      nvim->err_write("Some arguments to nvim were not strings.\n");
    }
  }
  emit signaller.editor_spawned(nvim_path, std::move(capabilities), std::move(args));
}

std::string QtEditorUIBase::current_dir() const { return cwd; }

QWidget* QtEditorUIBase::widget() { return &inheritor; }
