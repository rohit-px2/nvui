#include "editor_base.hpp"
#include <fmt/core.h>
#include <fmt/format.h>

static const Nvim::ClientInfo nvui_cinfo {
  "nvui", {0, 2, 2}, "ui", {}, {
    {"website", "https://github.com/rohit-px2/nvui"},
    {"license", "MIT"}
  }
};

EditorBase::EditorBase(
  std::string nvim_path,
  std::vector<std::string> nvim_args,
  QObject* thread_target_obj
)
: hl_state(), n_cursor(), popup_menu(nullptr),
  cmdline(nullptr), grids(),
  nvim(std::make_unique<Nvim>(nvim_path, nvim_args)),
  ext(), grids_need_ordering(false), ms_font_dimensions {10, 10},
  target_object(thread_target_obj)
{
  nvim->set_client_info(nvui_cinfo);
  nvim->set_var("nvui", 1);
  nvim->on_exit([this] {
    QMetaObject::invokeMethod(target_object, [this] {
      done = true;
      do_close();
    });
  });
}

EditorBase::~EditorBase() = default;

void EditorBase::setup()
{
  popup_menu = popup_new();
  cmdline = cmdline_new();
  register_handlers();
  popup_menu->register_nvim(*nvim);
  cmdline->register_nvim(*nvim);
  n_cursor.register_nvim(*nvim);
}

void EditorBase::confirm_qa()
{
  nvim->command("confirm qa");
}

void EditorBase::handle_redraw(Object msg)
{
  auto* arr = msg.array();
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
      func_it->second(span);
    }
  }
}

void EditorBase::set_handler(
  std::string name,
  HandlerFunc func
)
{
  handlers.emplace(std::move(name), std::move(func));
}

Color EditorBase::default_fg() const
{
  return hl_state.default_fg();
}

Color EditorBase::default_bg() const
{
  return hl_state.default_bg();
}

void EditorBase::send_redraw()
{
  for(auto& grid : grids) grid->send_redraw();
}

void EditorBase::register_handlers()
{
  // Set GUI handlers before we set the notification handler (since Nvim runs on a different thread,
  // it can be called any time)
  set_handler("hl_attr_define", [&](std::span<const Object> objs) {
    for(const auto& obj : objs) hl_state.define(obj);
  });
  set_handler("hl_group_set", [&](std::span<const Object> objs) {
    for (const auto& obj : objs) hl_state.group_set(obj);
  });
  set_handler("default_colors_set", [&](std::span<const Object> objs) {
    if (objs.empty()) return;
    hl_state.default_colors_set(objs.back());
    default_colors_changed(default_fg(), default_bg());
  });
  set_handler("grid_line", [&](std::span<const Object> objs) {
    grid_line(objs);
  });
  set_handler("option_set", [&](std::span<const Object> objs) {
    option_set(objs);
  });
  set_handler("grid_resize", [&](std::span<const Object> objs) {
    grid_resize(objs);
  });
  set_handler("flush", [&](std::span<const Object>) {
    flush();
  });
  set_handler("win_pos", [&](std::span<const Object> objs) {
    win_pos(objs);
  });
  set_handler("grid_clear", [&](std::span<const Object> objs) {
    grid_clear(objs);
  });
  set_handler("grid_cursor_goto", [&](std::span<const Object> objs) {
    grid_cursor_goto(objs);
  });
  set_handler("grid_scroll", [&](std::span<const Object> objs) {
    grid_scroll(objs);
  });
  set_handler("mode_info_set", [&](std::span<const Object> objs) {
    mode_info_set(objs);
  });
  set_handler("mode_change", [&](std::span<const Object> objs) {
    mode_change(objs);
  });
  set_handler("popupmenu_show", [&](std::span<const Object> objs) {
    popupmenu_show(objs);
  });
  set_handler("popupmenu_hide", [&](std::span<const Object> objs) {
    popupmenu_hide(objs);
  });
  set_handler("popupmenu_select", [&](std::span<const Object> objs) {
    popupmenu_select(objs);
  });
  set_handler("busy_start", [&](std::span<const Object>) {
    busy_start();
  });
  set_handler("busy_stop", [&](std::span<const Object>) {
    busy_stop();
  });
  set_handler("cmdline_show", [&](std::span<const Object> objs) {
    cmdline_show(objs);
  });
  set_handler("cmdline_hide", [&](std::span<const Object> objs) {
    cmdline_hide(objs);
  });
  set_handler("cmdline_pos", [&](std::span<const Object> objs) {
    cmdline_cursor_pos(objs);
  });
  set_handler("cmdline_special_char", [&](std::span<const Object> objs) {
    cmdline_special_char(objs);
  });
  set_handler("cmdline_block_show", [&](std::span<const Object> objs) {
    cmdline_block_show(objs);
  });
  set_handler("cmdline_block_append", [&](std::span<const Object> objs) {
    cmdline_block_append(objs);
  });
  set_handler("cmdline_block_hide", [&](std::span<const Object> objs) {
    cmdline_block_hide(objs);
  });
  set_handler("mouse_on", [&](std::span<const Object>) {
    set_mouse_enabled(true);
  });
  set_handler("mouse_off", [&](std::span<const Object>) {
    set_mouse_enabled(false);
  });
  set_handler("win_hide", [&](std::span<const Object> objs) {
    win_hide(objs);
  });
  set_handler("win_float_pos", [&](std::span<const Object> objs) {
    win_float_pos(objs);
  });
  set_handler("win_close", [&](std::span<const Object> objs) {
    win_close(objs);
  });
  set_handler("grid_destroy", [&](std::span<const Object> objs) {
    grid_destroy(objs);
  });
  set_handler("msg_set_pos", [&](std::span<const Object> objs) {
    msg_set_pos(objs);
  });
  set_handler("win_viewport", [&](std::span<const Object> objs) {
    win_viewport(objs);
  });
  nvim->set_notification_handler("redraw", [this](Object msg) {
    QMetaObject::invokeMethod(target_object, [this, o = std::move(msg)] {
      handle_redraw(std::move(o));
    });
  });
}

void EditorBase::set_mouse_enabled(bool enabled) { enable_mouse = enabled; }

void EditorBase::destroy_grid(u64 grid_num)
{
  auto it = std::find_if(grids.begin(), grids.end(), [=](const auto& g) {
    return g->id == grid_num;
  });
  if (it == grids.end()) return;
  grids.erase(it);
}

GridBase* EditorBase::find_grid(i64 grid_num)
{
  for(const auto& grid : grids)
  {
    if (grid->id == grid_num) return grid.get();
  }
  return nullptr;
}

void EditorBase::flush()
{
  if (grids_need_ordering) order_grids();
  redraw();
}

i64 EditorBase::get_win(const NeovimExt& ext) const
{
  using namespace std;
  vector<uint8_t> data;
  for(const auto& c : ext.data) data.push_back(static_cast<uint8_t>(c));
  const auto size = ext.data.size();
  if (size == 1 && data[0] <= 0x7f) return (int) data[0];
  else if (size == 1 && data[0] >= 0xe0)
  {
    return int8_t(data[0]);
  }
  else if (size == 2 && data[0] == 0xcc) return (int) data[1];
  else if (size == 2 && data[0] == 0xd0) return int8_t(data[1]);
  else if (size == 3 && data[0] == 0xcd)
  {
    return u16(data[2]) | u16(data[1]) << 8;
  }
  else if (size == 3 && data[0] == 0xd1)
  {
    return int16_t(uint16_t(data[2])) | uint16_t(data[1]) << 8;
  }
  else if (size == 5 && data[0] == 0xce)
  {
    return u32(data[4] | u32(data[3]) << 8
         | u32(data[2]) << 16 | u32(data[1]) << 24);
  }
  return numeric_limits<int>::min();
}

void EditorBase::order_grids()
{
  using GridPtr = const std::unique_ptr<GridBase>&;
  std::sort(grids.begin(), grids.end(), [](GridPtr g1, GridPtr g2) {
    return *g1 < *g2;
  });
  grids_need_ordering = false;
}

void EditorBase::screen_resized(int sw, int sh)
{
  if (sw <= 0 || sh <= 0) return;
  pixel_dimensions = {sw, sh};
  auto [font_width, font_height] = font_dimensions();
  int cols = sw / font_width;
  int rows = sh / font_height;
  cmdline->editor_resized(sw, sh);
  dimensions = {cols, rows};
  nvim->resize(cols, rows);
}

const HLState& EditorBase::hlstate() const { return hl_state; }

void EditorBase::nvim_ui_attach(
  int width,
  int height,
  std::unordered_map<std::string, bool> capabilities
)
{
  nvim->attach_ui(height, width, std::move(capabilities));
}


void EditorBase::grid_line(std::span<const Object> objs)
{
  int hl_id = 0;
  for(auto& grid_cmd : objs)
  {
    auto* arr = grid_cmd.array();
    assert(arr && arr->size() >= 4);
    const auto grid_num = arr->at(0).get<u64>();
    GridBase* grid_ptr = find_grid(grid_num);
    if (!grid_ptr) continue;
    if (!grid_ptr) return;
    GridBase& g = *grid_ptr;
    int start_row = (int) arr->at(1);
    int start_col = (int) arr->at(2);
    int col = start_col;
    auto cells = arr->at(3).array();
    assert(cells);
    for(auto& cell : *cells)
    {
      assert(cell.has<ObjectArray>());
      auto& cell_arr = cell.get<ObjectArray>();
      assert(cell_arr.size() >= 1 && cell_arr.size() <= 3);
      // [text, (hl_id, repeat)]
      int repeat = 1;
      assert(cell_arr.at(0).is_string());
      grid_char text = GridChar::grid_char_from_str(cell_arr[0].get<std::string>());
      // If the previous char was a double-width char,
      // the current char is an empty string.
      bool prev_was_dbl = text.isEmpty();
      bool is_dbl = false;
      if (prev_was_dbl)
      {
        std::size_t idx = start_row * grid_ptr->cols + col - 1;
        if (idx < grid_ptr->area.size())
        {
          grid_ptr->area[idx].double_width = true;
        }
      }
      switch(cell_arr.size())
      {
        case 2:
          hl_id = (int) cell_arr.at(1);
          break;
        case 3:
          hl_id = (int) cell_arr.at(1);
          repeat = (int) cell_arr.at(2);
          break;
      }
      g.set_text(std::move(text), start_row, col, hl_id, repeat, is_dbl);
      col += repeat;
    }
    g.send_draw({start_col, start_row, (col - start_col), 1});
  }
}



static FontDesc parse_font(const QString& str)
{
  const QStringList list = str.split(":");
  // 1st is font name, 2nd is size, anything after is bold/italic specifier
  switch(list.size())
  {
    case 1:
      return {list.at(0).toStdString(), -1, FontOpts::Normal};
    case 2:
    {
      // Substr excluding the first char ('h')
      // to get the number
      const QStringView size_str {list.at(1).utf16() + 1, list[1].size() - 1};
      return {list.at(0).toStdString(), size_str.toDouble(), FontOpts::Normal};
    }
    default:
    {
      const QStringView size_str {list.at(1).utf16() + 1, list[1].size() - 1};
      FontOptions font_opts = FontOpts::Normal;
      for(std::uint8_t i = 0; i < list.size(); ++i)
      {
        if (list.at(i) == QLatin1String("b"))
        {
          font_opts |= FontOpts::Bold;
        }
        else if (list.at(i) == QLatin1String("i"))
        {
          font_opts |= FontOpts::Italic;
        }
        else if (list.at(i) == QLatin1String("u"))
        {
          font_opts |= FontOpts::Underline;
        }
        else if (list.at(i) == QLatin1String("s"))
        {
          font_opts |= FontOpts::Strikethrough;
        }
      }
      return {list.at(0).toStdString(), size_str.toDouble(), font_opts};
    }
  }
}

static std::vector<FontDesc> parse_guifont(std::string gfdesc)
{
  std::vector<FontDesc> font_descriptions;
  // Makes it easier to split/replace strings
  QString guifont = QString::fromStdString(gfdesc);
  guifont.replace('_', ' ');
  if (guifont.isEmpty()) guifont = default_font_family();
  auto lst = guifont.split(',');
  if (lst.empty()) return {};
  for(const auto& str : lst)
  {
    font_descriptions.emplace_back(parse_font(str));
  }
  if (!font_descriptions.empty())
  {
    // uniform point size. This is going to be the last
    // font in the list so that point size can be set programatically
    auto ps = font_descriptions.back().point_size;
    for(auto& fdesc : font_descriptions) fdesc.point_size = ps;
  }
  return font_descriptions;
}

void EditorBase::option_set(std::span<const Object> objs)
{
  const auto extension_for = [&](const auto& s) -> bool* {
    if (s == "ext_linegrid") return &ext.linegrid;
    else if (s == "ext_popupmenu") return &ext.popupmenu;
    else if (s == "ext_cmdline") return &ext.cmdline;
    else if (s == "ext_multigrid") return &ext.multigrid;
    else if (s == "ext_wildmenu") return &ext.wildmenu;
    else if (s == "ext_messages") return &ext.messages;
    else return nullptr;
  };
  for(const auto& obj : objs)
  {
    auto* arr = obj.array();
    if (!(arr && arr->size() >= 2)) continue;
    const auto& key = arr->at(0).get<std::string>();
    const auto value = arr->at(1);
    auto* opt = extension_for(key);
    if (opt && value.boolean()) *opt = *value.boolean();
    if (key == "guifont" && value.is_string())
    {
      guifonts = parse_guifont(value.get<std::string>());
      set_fonts(guifonts);
      send_redraw();
    }
    field_updated(key, value);
  }
}

void EditorBase::grid_resize(std::span<const Object> objs)
{
  // Should only run once
  for(const auto& obj : objs)
  {
    auto vars = obj.try_decompose<u64, u64, u64>();
    if (!vars) continue;
    auto [grid_num, width, height] = *vars;
    assert(grid_num != 0);
    GridBase* grid = find_grid(grid_num);
    if (grid)
    {
      grid->set_size(width, height);
    }
    else
    {
      create_grid(0, 0, width, height, grid_num);
      grid = find_grid(grid_num);
      grids_need_ordering = true;
      // Created grid appears above all others
    }
    grid->send_redraw();
  }
}

void EditorBase::win_pos(std::span<const Object> objs)
{
  for(const auto& obj : objs)
  {
    auto vars = obj.try_decompose<u64, NeovimExt, u64, u64, u64, u64>();
    if (!vars) continue;
    const auto& [grid_num, win, sr, sc, width, height] = *vars;
    GridBase* grid = find_grid(grid_num);
    if (!grid)
    {
      fmt::print("No grid #{} found.\n", grid_num);
      continue;
    }
    grid->hidden = false;
    grid->win_pos(sc, sr);
    grid->set_size(width, height);
    grid->winid = get_win(win);
    grids_need_ordering = true;
  }
  send_redraw();
}

void EditorBase::grid_clear(std::span<const Object> objs)
{
  for(const auto& obj : objs)
  {
    auto* arr = obj.array();
    assert(arr && arr->size() >= 1);
    const auto grid_num = arr->at(0).u64();
    assert(grid_num);
    auto* grid = find_grid(*grid_num);
    if (!grid) continue;
    grid->clear();
  }
}

void EditorBase::grid_cursor_goto(std::span<const Object> objs)
{
  if (objs.empty()) return;
  const auto& obj = objs.back();
  auto vars = obj.try_decompose<u16, int, int>();
  if (!vars) return;
  auto [grid_num, row, col] = *vars;
  GridBase* grid = find_grid(grid_num);
  if (!grid) return;
  n_cursor.go_to({grid_num, grid->x, grid->y, row, col});
  cursor_moved();
}

void EditorBase::grid_scroll(std::span<const Object> objs)
{
  for(const auto& obj : objs)
  {
    auto vars = obj.try_decompose<u16, u16, u16, u16, u16, int>();
    if (!vars) continue;
    const auto [grid_num, top, bot, left, right, rows] = *vars;
    // const int cols = arr->at(6);
    GridBase* grid = find_grid(grid_num);
    if (!grid) continue;
    grid->scroll(top, bot, left, right, rows);
  }
}

void EditorBase::mode_info_set(std::span<const Object> objs)
{
  n_cursor.mode_info_set(objs);
}

void EditorBase::mode_change(std::span<const Object> objs)
{
  n_cursor.mode_change(objs);
}

void EditorBase::popupmenu_show(std::span<const Object> objs)
{
  if (objs.empty()) return;
  auto arr = objs.back().array();
  if (!(arr && arr->size() >= 5)) return;
  auto font_dims = font_dimensions();
  if (!arr->at(0).is_array()) return;
  const auto& items = arr->at(0).get<ObjectArray>();
  auto selected = arr->at(1).try_convert<int>().value_or(-1);
  auto row = arr->at(2).try_convert<int>().value_or(0);
  auto col = arr->at(3).try_convert<int>().value_or(0);
  auto grid_num = arr->at(4).try_convert<int>().value_or(0);
  auto* grid = find_grid(grid_num);
  int grid_x = 0;
  int grid_y = 0;
  if (grid)
  {
    grid_x = grid->x;
    grid_y = grid->y;
  }
  popup_menu->pum_show(
    items, selected, grid_num, row, col, font_dims, grid_x, grid_y
  );
}

void EditorBase::popupmenu_hide(std::span<const Object> objs)
{
  popup_menu->pum_hide(objs);
}

void EditorBase::popupmenu_select(std::span<const Object> objs)
{
  popup_menu->pum_sel(objs);
}

void EditorBase::cmdline_show(std::span<const Object> objs)
{
  cmdline->cmdline_show(objs);
  popup_menu->attach_cmdline(cmdline.get());
}

void EditorBase::cmdline_hide(std::span<const Object> objs)
{
  cmdline->cmdline_hide(objs);
  popup_menu->detach_cmdline();
}

void EditorBase::cmdline_cursor_pos(std::span<const Object> objs)
{
  cmdline->cmdline_cursor_pos(objs);
}

void EditorBase::cmdline_special_char(std::span<const Object> objs)
{
  cmdline->cmdline_special_char(objs);
}

void EditorBase::cmdline_block_show(std::span<const Object> objs)
{
  cmdline->cmdline_block_show(objs);
}

void EditorBase::cmdline_block_append(std::span<const Object> objs)
{
  cmdline->cmdline_block_append(objs);
}

void EditorBase::cmdline_block_hide(std::span<const Object> objs)
{
  cmdline->cmdline_block_hide(objs);
}

void EditorBase::win_hide(std::span<const Object> objs)
{
  for(const auto& obj : objs)
  {
    auto* arr = obj.array();
    assert(arr && arr->size() >= 2);
    auto grid_num = arr->at(0).u64();
    if (!grid_num) continue;
    if (GridBase* grid = find_grid(*grid_num)) grid->hidden = true;
  }
}

void EditorBase::win_float_pos(std::span<const Object> objs)
{
  for(const auto& obj : objs)
  {
    auto vars = obj.try_decompose<u64, NeovimExt, std::string, u64, double, double>();
    if (!vars) continue;
    auto [grid_num, win, anchor_dir, anchor_grid_num, anchor_row, anchor_col] = *vars;
    int zindex = -1;
    if (auto* params = obj.array(); params && params->size() >= 8)
    {
      zindex = params->at(7).try_convert<int>().value_or(-1);
    }
    QPointF anchor_rel(anchor_col, anchor_row);
    //bool focusable = arr.ptr[6].as<bool>();
    GridBase* grid = find_grid(grid_num);
    GridBase* anchor_grid = find_grid(anchor_grid_num);
    if (!grid || !anchor_grid) continue;
    // Anchor dir is "NW", "SW", "SE", "NE"
    // The anchor direction indicates which corner of the grid
    // should be at the position given.
    QPoint anchor_pos = anchor_grid->top_left() + anchor_rel.toPoint();
    if (anchor_dir == "SW")
    {
      anchor_pos -= QPoint(0, grid->rows);
    }
    else if (anchor_dir == "SE")
    {
      anchor_pos -= QPoint(grid->cols, grid->rows);
    }
    else if (anchor_dir == "NE")
    {
      anchor_pos -= QPoint(grid->cols, 0);
    }
    else
    {
      // NW, no need to do anything
    }
    if (!popup_menu->hidden() && popup_menu->selected_idx() != -1)
    {
      // Anchor grid to top-right of popup menu
      // top-right of popup menu is the top-left of the popup menu
      // grid
      using std::round;
      using std::ceil;
      auto [width, height] = font_dimensions();
      auto pum_rect = popup_menu->get_rect();
      auto pum_tr = pum_rect.topRight();
      anchor_pos = QPoint(ceil(pum_tr.x() / width), round(pum_tr.y() / height));
    }
    grid->winid = get_win(win);
    grid->float_pos(anchor_pos.x(), anchor_pos.y());
    QPointF absolute_win_pos = anchor_rel + QPointF {anchor_col, anchor_row};
    grid->set_float_ordering_info(zindex, absolute_win_pos);
    grids_need_ordering = true;
  }
}

void EditorBase::win_close(std::span<const Object> objs)
{
  for(const auto& obj : objs)
  {
    auto* arr = obj.array();
    if (!(arr && arr->size() >= 1)) continue;
    auto grid_num = arr->at(0).u64();
    if (!grid_num) continue;
    destroy_grid(*grid_num);
  }
  send_redraw();
}

void EditorBase::grid_destroy(std::span<const Object> objs)
{
  for(const auto& obj : objs)
  {
    auto& arr = obj.get<ObjectArray>();
    assert(arr.size() >= 1);
    auto grid_num = arr.at(0).get<u64>();
    destroy_grid(grid_num);
  }
  send_redraw();
}

void EditorBase::msg_set_pos(std::span<const Object> objs)
{
  for(const auto& obj : objs)
  {
    auto vars = obj.try_decompose<u64, u64>();
    if (!vars) continue;
    auto [grid_num, row] = *vars;
    //auto scrolled = arr.ptr[2].as<bool>();
    //auto sep_char = arr.ptr[3].as<QString>();
    if (GridBase* grid = find_grid(grid_num))
    {
      grid->msg_set_pos(grid->x, row);
      grids_need_ordering = true;
    }
  }
  send_redraw();
}

void EditorBase::win_viewport(std::span<const Object> objs)
{
  for(const auto& obj : objs)
  {
    auto vars = obj.try_decompose<u64, NeovimExt, u32, u32, u32, u32>();
    if (!vars) continue;
    auto [grid_num, ext, topline, botline, curline, curcol] = *vars;
    Viewport vp = {topline, botline, curline, curcol};
    auto* grid = find_grid(grid_num);
    if (!grid) continue;
    grid->viewport_changed(std::move(vp));
  }
}

EditorBase::NvimDimensions EditorBase::nvim_dimensions() const
{
  return {dimensions.width(), dimensions.height()};
}

bool EditorBase::mouse_enabled() const { return enable_mouse; }

void EditorBase::create_grid(u32 x, u32 y, u32 w, u32 h, u64 id)
{
  grids.emplace_back(std::make_unique<GridBase>(x, y, w, h, id));
}

void EditorBase::field_updated(std::string_view, const Object&) {}

bool EditorBase::nvim_exited() const { return done; }

FontDimensions EditorBase::font_dimensions() const
{
  return ms_font_dimensions;
}

void EditorBase::set_font_dimensions(float width, float height)
{
  if (width != ms_font_dimensions.width || height != ms_font_dimensions.height)
  {
    ms_font_dimensions = {width, height};
    screen_resized(pixel_dimensions.width(), pixel_dimensions.height());
  }
}
