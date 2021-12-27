#include "popupmenu.hpp"
#include <iostream>
#include <QPainter>
#include <QStringBuilder>
#include <QStringLiteral>
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
  is_hidden = false;
  completion_items.clear();
  for(std::size_t i = 0; i < objs.size(); ++i)
  {
    auto& obj = objs[i];
    auto* arr = obj.array();
    assert(arr && arr->size() >= 5);
    auto* items = arr->at(0).array();
    cur_selected = (int) arr->at(1);
    row = (int) arr->at(2);
    col = (int) arr->at(3);
    grid_num = (int) arr->at(4);
    assert(items);
    add_items(*items);
    if (cur_selected >= 0 && cur_selected < int(completion_items.size()))
    {
      completion_items[i].selected = true;
    }
  }
  redraw();
}

void PopupMenu::pum_sel(std::span<const Object> objs)
{
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

PopupMenuQ::PopupMenuQ(const HLState* state, QWidget* parent)
  : PopupMenu(state), QWidget(parent), icon_manager(10)
{
  hide();
}

PopupMenuQ::~PopupMenuQ() = default;

void PopupMenuQ::paint()
{
  update_highlight_attributes();
  if (completion_items.empty())
  {
    hide();
    return;
  }
  QPainter p(&pixmap);
  int cur_y = std::ceil(border_width);
  bool nothing_selected = cur_selected == -1;
  if (nothing_selected) cur_selected = 0;
  for(std::size_t i = 0; i < completion_items.size(); ++i)
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
  const auto* icon_ptr = icon_manager.icon_for_kind(item.kind);
  int left = std::ceil(border_width);
  p.fillRect(left, y, pixmap.width(), dimensions.height, bg.qcolor());
  if (icon_ptr && icons_enabled)
  {
    p.drawPixmap(QPoint {left, y}, *icon_ptr);
    left += icon_ptr->width() * icon_space;
  }
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
  double text_width = 0.;
  for(const auto& item : completion_items)
  {
    text_width = std::max(text_width, metrics.horizontalAdvance(item.word));
  }
  // Box width = max number of characters.
  // Multiply by font dimensions to get the number of pixels wide.
  // Also add in the width of an icon.
  float width =
    text_width * dimensions.width + icon_manager.icon_size()
    + (border_width * 2);
  if (attached_width) width = float(attached_width.value());
  float height = completion_items.size() * dimensions.height
    + (border_width * 2);
  pixmap = QPixmap(width, height);
	resize(width, height);
  pixmap.fill(hl_state->colors_for(*pmenu).bg.qcolor());
}

QRect PopupMenuQ::get_rect() const { return rect(); }

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
