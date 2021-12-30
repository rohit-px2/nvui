#include "popupmenu.hpp"
#include <iostream>
#include <QPainter>
#include <QStringBuilder>
#include <QStringLiteral>
#include "cmdline.hpp"
#include "hlstate.hpp"
#include "msgpack_overrides.hpp"
#include "nvim.hpp"
#include "nvim_utils.hpp"
#include "utils.hpp"

QString PopupMenuIconManager::kind_to_iname(QString kind)
{
  if (kind.size() <= 0) return QString();
  int ws_index = kind.indexOf(' ');
  if (ws_index != -1) kind = kind.right(kind.size() - ws_index - 1);
  if (kind == "EnumMember") return QStringLiteral("enum-member");
  else
  {
    kind[0] = kind[0].toLower();
    return kind;
  }
}

QString PopupMenuIconManager::iname_to_kind(const QString& iname)
{
  if (iname.size() <= 0) return QString();
  if (iname == "enum-member")
  {
    return QStringLiteral("EnumMember");
  }
  return iname.at(0).toUpper() % iname.right(iname.size() - 1);
}

using fg_bg = PopupMenuIconManager::fg_bg;
static std::pair<QColor, QColor> find_or_default(
  const QHash<QString, fg_bg>& map,
  const QString& key,
  const QColor& default_fg,
  const QColor& default_bg
)
{
  const auto it = map.find(key);
  if (it != map.end()) {
    return {it->first.value_or(default_fg), it->second.value_or(default_bg)};
  }
  else return {default_fg, default_bg};
}

QPixmap PopupMenuIconManager::load_icon(const QString& iname, int width)
{
  auto&& [fg, bg] = find_or_default(colors, iname, default_fg, default_bg);
  auto&& px = pixmap_from_svg(
    constants::picon_fp() % iname % ".svg",
    fg,
    Qt::transparent,
    width, width
  );
  return px.value_or(QPixmap());
}

void PopupMenuIconManager::load_icons(int width)
{
  auto keys = icons.keys();
  for(auto& key : keys)
  {
    icons[key] = load_icon(key, width);
  }
}

const QPixmap* PopupMenuIconManager::icon_for_kind(const QString& kind)
{
  if (kind.isEmpty()) return nullptr;
  QString iname = kind_to_iname(kind).trimmed();
  const auto it = icons.find(iname);
  if (it == icons.end()) return nullptr;
  else return &(*it);
}

PopupMenu::PopupMenu(const HLState* state)
: hl_state(state)
{
}

void PopupMenu::pum_show(std::span<const Object> objs)
{
  if (objs.empty()) return;
  const auto& arr = objs.back().array();
  if (!(arr && arr->size() >= 5)) return;
  longest_word_size = 0;
  is_hidden = false;
  completion_items.clear();
  if (!arr->at(0).is_array()) return;
  const auto& items = arr->at(0).get<ObjectArray>();
  auto selected = arr->at(1).try_convert<int>().value_or(-1);
  auto row = arr->at(2).try_convert<int>().value_or(0);
  auto col = arr->at(3).try_convert<int>().value_or(0);
  auto grid_num = arr->at(4).try_convert<int>().value_or(0);
  pum_show(items, selected, grid_num, row, col, {});
}

void PopupMenu::pum_sel(std::span<const Object> objs)
{
  update_highlight_attributes();
  if (objs.empty()) return;
  const auto& obj = objs.back();
  auto* arr = obj.array();
  assert(arr && arr->size() >= 1);
  if (cur_selected >= 0) completion_items[cur_selected].selected = false;
  cur_selected = static_cast<int>(arr->at(0));
  if (cur_selected >= 0) completion_items[cur_selected].selected = true;
  redraw();
}

void PopupMenu::pum_hide(std::span<const Object>)
{
  is_hidden = true;
  do_hide();
}

void PopupMenu::pum_show(
  const ObjectArray& items,
  int selected,
  int p_grid_num,
  int p_row,
  int p_col,
  FontDimensions dims,
  int p_grid_x,
  int p_grid_y
)
{
  parent_dims = dims;
  longest_word_size = 0;
  is_hidden = false;
  completion_items.clear();
  add_items(items);
  if (selected >= 0 && selected < int(completion_items.size()))
  {
    completion_items[selected].selected = true;
  }
  row = p_row;
  col = p_col;
  grid_num = p_grid_num;
  grid_x = p_grid_x;
  grid_y = p_grid_y;
  if (grid_x >= 0 && grid_y >= 0)
  {
    pixel_x = (grid_x + col) * dims.width;
    pixel_y = (grid_y + row) * dims.height;
  }
  else
  {
    pixel_x = -1;
    pixel_y = -1;
  }
  update_highlight_attributes();
  update_dimensions();
  redraw();
  do_show();
}

void PopupMenu::add_items(const ObjectArray& items)
{
  for(const auto& item : items)
  {
    auto* arr = item.array();
    assert(arr && arr->size() >= 4);
    auto word = QString::fromStdString(*arr->at(0).string());
    auto kind = QString::fromStdString(*arr->at(1).string());
    auto menu = QString::fromStdString(*arr->at(2).string());
    auto info = QString::fromStdString(*arr->at(3).string());
    longest_word_size = std::max(longest_word_size, word.size());
    completion_items.push_back({
      false, std::move(word), std::move(kind), std::move(menu), std::move(info)
    });
  }
}

void PopupMenu::update_highlight_attributes()
{
  pmenu = &hl_state->attr_for_id(hl_state->id_for_name("Pmenu"));
  pmenu_sbar = &hl_state->attr_for_id(hl_state->id_for_name("PmenuSbar"));
  pmenu_sel = &hl_state->attr_for_id(hl_state->id_for_name("PmenuSel"));
  pmenu_thumb = &hl_state->attr_for_id(hl_state->id_for_name("PmenuThumb"));
}

QRect PopupMenu::calc_rect(int width, int height, int maxheight) const
{
  if (cmdline)
  {
    auto r = cmdline->get_rect();
    return QRect {r.bottomLeft(), QSize {r.width(), height}};
  }
  else
  {
    auto fheight = parent_dims.height;
    int x = (grid_x + col) * parent_dims.width;
    int y = (grid_y + row + 1) * fheight;
    // Prefer showing the popup menu under
    if (y + height > maxheight && y - fheight - height >= 0)
    {
      y -= (fheight + height);
    }
    else if (y + height > maxheight && y - fheight - height < 0
        && (y + height - maxheight) > -(y - fheight - height))
    {
      y = 0;
      height = (grid_y + row) * fheight;
    }
    return {x, y, width, height};
  }
}

PopupMenuQ::PopupMenuQ(const HLState* state, QWidget* parent)
  : PopupMenu(state), QWidget(parent), icon_manager(10)
{
  hide();
}

PopupMenuQ::~PopupMenuQ() = default;

std::size_t PopupMenuQ::max_possible_items() const
{
  if (auto* parent = parentWidget())
  {
    return parent->height() / dimensions.height;
  }
  return std::numeric_limits<std::size_t>::max();
}

int PopupMenuQ::max_items() const
{
  return pixmap.height() / dimensions.height;
}

void PopupMenuQ::paint()
{
  if (completion_items.empty())
  {
    hide();
    return;
  }
  QPainter p(&pixmap);
  p.setFont(pmenu_font);
  int cur_y = std::ceil(border_width);
  bool nothing_selected = cur_selected == -1;
  if (nothing_selected) cur_selected = 0;
  size_t maxitems = pixmap.height() / dimensions.height;
  for(std::size_t i = 0; i < maxitems; ++i)
  {
    std::size_t index = (cur_selected + i) % completion_items.size();
    if (completion_items[index].selected)
    {
      const auto& item = completion_items[index];
      draw_with_attr(p, *pmenu_sel, item, cur_y);
    }
    else
    {
      draw_with_attr(p, *pmenu, completion_items[index], cur_y);
    }
    cur_y += dimensions.height;
  }
  if (nothing_selected) cur_selected = -1;
  update();
}

void PopupMenuQ::font_changed(const QFont& font, FontDimensions dims)
{
  update_highlight_attributes();
  pmenu_font = font;
  dimensions = dims;
  QFontMetricsF fm {font};
  linespace = dimensions.height - fm.height();
  font_ascent = fm.ascent();
  icon_manager.size_changed(dimensions.height + icon_size_offset);
  update_dimensions();
}

void PopupMenuQ::draw_with_attr(QPainter& p, const HLAttr& attr, const PMenuItem& item, int y)
{
  float offset = font_ascent + (linespace / 2.f);
  auto [fg, bg, sp] = attr.fg_bg_sp(hl_state->default_colors_get());
  font::set_opts(pmenu_font, attr.font_opts);
  p.setFont(pmenu_font);
  const auto* icon_ptr = icon_manager.icon_for_kind(item.kind);
  int left = std::ceil(border_width);
  p.fillRect(left, y, pixmap.width(), dimensions.height, bg.qcolor());
  if (icon_ptr && icons_enabled)
  {
    p.drawPixmap(QPoint {left, y}, *icon_ptr);
    left += icon_ptr->width() * icon_space;
  }
  p.setPen(fg.qcolor());
  p.drawText(QPoint(left, y + offset), item.word);
}

PopupMenu::Rectangle
PopupMenuQ::dimensions_for(int x, int y, int sw, int sh)
{
  auto popup_width = width();
  if (x + popup_width > sw && x - popup_width >= 0) x -= popup_width;
  if (y + height() > sh && y - height() >= 0) y -= height();
  return {x, y, popup_width, height()};
}

void PopupMenuQ::redraw()
{
  paint();
}

void PopupMenuQ::paintEvent(QPaintEvent*)
{
  if (cmdline) move(cmdline->get_rect().bottomLeft());
  QPainter p(this);
  p.drawPixmap(rect(), pixmap, rect());
  QPen pen {border_color.qcolor()};
  pen.setWidth(border_width);
  p.setPen(std::move(pen));
  QRect draw_rect = rect();
  int offset = std::ceil(float(border_width) / 2.f);
  draw_rect.adjust(0, 0, -offset, -offset);
  p.drawRect(draw_rect);
}

void PopupMenuQ::update_dimensions()
{
  QFontMetricsF metrics {pmenu_font};
  double text_width = longest_word_size * metrics.horizontalAdvance('W');
  int parent_height = parentWidget() ? parentWidget()->height() : INT_MAX;
  int parent_width = parentWidget() ? parentWidget()->width() : INT_MAX;
  // Box width = max number of characters.
  // Multiply by font dimensions to get the number of pixels wide.
  // Also add in the width of an icon.
  int width =
    text_width + icon_manager.icon_size()
    + (border_width * 2);
  if (attached_width) width = float(attached_width.value());
  width = std::min(width, parent_width);
  int unconstrained_pmenu_height = completion_items.size() * dimensions.height;
  int height = std::min(unconstrained_pmenu_height, parent_height);
  height /= dimensions.height;
  height *= dimensions.height;
  height += border_width * 2;
  if (pixmap.size() == QSize(width, height)) return;
  width = std::max(double(width), metrics.averageCharWidth() * 15);
  auto rect = calc_rect(width, height, parent_height);
  width = rect.width();
  height = rect.height();
  move(rect.topLeft());
  if (QSize(width, height) == size()) return;
  pixmap = QPixmap(width, height);
  resize(width, height);
  if (pmenu) pixmap.fill(hl_state->colors_for(*pmenu).bg.qcolor());
}

void PopupMenu::attach_cmdline(Cmdline* cmd)
{
  cmdline = cmd;
  update_dimensions();
  redraw();
}

void PopupMenu::detach_cmdline()
{
  attached_width.reset();
  cmdline = nullptr;
  update_dimensions();
}

QRect PopupMenuQ::get_rect() const { return rect(); }

void PopupMenuQ::do_show() { show(); }
void PopupMenuQ::do_hide() { hide(); }

void PopupMenuQ::register_nvim(Nvim& nvim)
{
  const auto on = [&](auto... p) {
    listen_for_notification(nvim, p..., this);
  };
  on(
    "NVUI_PUM_ICONS_TOGGLE", [this](const auto&) {
    toggle_icons_enabled();
  });
  on("NVUI_PUM_ICON_OFFSET", paramify<int>([this](int offset) {
    set_icon_size_offset(offset);
  }));
  on("NVUI_PUM_ICON_SPACING", paramify<float>([this](float spacing) {
    set_icon_space(spacing);
  }));
   //:call rpcnotify(1, 'NVUI_PUM_ICON_FG', '(iname)', '(background color)')
  on("NVUI_PUM_ICON_BG",
    paramify<QString, QString>([this](QString icon_name, QString color_str) {
    if (!QColor::isValidColor(color_str)) return;
    set_icon_bg(std::move(icon_name), {color_str});
  }));
  // :call rpcnotify(1, 'NVUI_PUM_ICON_FG', '(iname)', '(foreground color)')
  on("NVUI_PUM_ICON_FG",
    paramify<QString, QString>([this](QString icon_name, QString color_str) {
      if (!QColor::isValidColor(color_str)) return;
      set_icon_fg(std::move(icon_name), {color_str});
  }));
  on("NVUI_PUM_ICON_COLORS",
    paramify<QString, QString, QString>([this](QString icon_name, QString fg_str, QString bg_str) {
      if (!QColor::isValidColor(fg_str) || !QColor::isValidColor(bg_str)) return;
      QColor fg {fg_str}, bg {bg_str};
      set_icon_colors(icon_name, fg, bg);
  }));
  on("NVUI_PUM_DEFAULT_ICON_FG", paramify<QString>([this](QString fg_str) {
    if (!QColor::isValidColor(fg_str)) return;
    set_default_icon_fg({fg_str});
  }));
  on("NVUI_PUM_DEFAULT_ICON_BG", paramify<QString>([this](QString bg_str) {
    if (!QColor::isValidColor(bg_str)) return;
    set_default_icon_bg({bg_str});
  }));
  using namespace std;
  handle_request<vector<string>, string>(nvim, "NVUI_POPUPMENU_ICON_NAMES",
    [&](const auto&) {
      return tuple {icon_list(), std::nullopt};
  }, this);
}
