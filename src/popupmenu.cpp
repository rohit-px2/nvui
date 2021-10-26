#include "popupmenu.hpp"
#include <iostream>
#include <QPainter>
#include <QStringBuilder>
#include <QStringLiteral>
#include "hlstate.hpp"
#include "msgpack_overrides.hpp"
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

PopupMenuInfo::PopupMenuInfo(PopupMenu* parent)
  : QWidget(parent->parentWidget()),
    parent_menu(parent)
{
  hide();
}

void PopupMenuInfo::draw(QPainter& p, const HLAttr& attr, const QString& info)
{
  Q_UNUSED(p);
  setFont(parent_menu->pmenu_font);
  current_attr = attr;
  current_text = info;
  update();
}

void PopupMenuInfo::paintEvent(QPaintEvent* event)
{
  Q_UNUSED(event);
  if (current_text.isNull()) hide();
  auto cell_width = parent_menu->cell_width;
  auto b_width = parent_menu->border_width;
  auto full_width = cell_width * cols + parent_menu->border_width * 2;
  resize(full_width, parent_menu->height());
  auto offset = std::ceil(parent_menu->border_width / 2.f);
  QPainter p(this);
  QFont p_font = font();
  p_font.setBold(current_attr.font_opts & FontOpts::Bold);
  p_font.setItalic(current_attr.font_opts & FontOpts::Italic);
  p_font.setUnderline(current_attr.font_opts & FontOpts::Underline);
  p_font.setStrikeOut(current_attr.font_opts & FontOpts::Strikethrough);
  p.setFont(p_font);
  p.fillRect(rect(), current_attr.bg().value_or(QColor(Qt::white).rgb()).qcolor());
  p.setPen({parent_menu->border_color, parent_menu->border_width});
  QRect r = rect();
  r.adjust(offset, offset, -offset, -offset);
  p.drawRect(r);
  QRect inner_rect = rect().adjusted(b_width, b_width, -b_width, -b_width);
  auto fg = current_attr.fg().value_or(uint32(0)).qcolor();
  p.setPen(fg);
  p.drawText(inner_rect, Qt::TextWordWrap, current_text);
}

PopupMenu::PopupMenu(const HLState* state, QWidget* parent)
  : QWidget(parent),
    hl_state(state),
    pixmap(),
    completion_items(max_items),
    info_widget(this),
    icon_manager(10),
    pmenu_font()
{
  // Without this flag, flickering occurs on WinEditorArea.
  setAttribute(Qt::WA_NativeWindow);
  hide();
}

void PopupMenu::pum_show(std::span<const Object> objs)
{
  is_hidden = false;
  completion_items.clear();
  using std::tuple;
  using std::vector;
  using std::string;
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
    paint();
  }
  resize(available_rect().size());
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
  paint();
}

void PopupMenu::pum_hide(std::span<const Object> objs)
{
  Q_UNUSED(objs);
  is_hidden = true;
  setVisible(false);
  info_widget.hide();
}

void PopupMenu::add_items(const ObjectArray& items)
{
  for(const auto& item : items)
  {
    auto* arr = item.array();
    assert(arr && arr->size() >= 4);
    auto* word = arr->at(0).string();
    auto* kind = arr->at(1).string();
    auto* menu = arr->at(2).string();
    auto* info = arr->at(3).string();
    assert(word && kind && menu && info);
    completion_items.push_back({
      false, *word, *kind, *menu,*info
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

void PopupMenu::paint()
{
  update_highlight_attributes();
  QPainter p(&pixmap);
  int cur_y = std::ceil(border_width);
  bool nothing_selected = cur_selected == -1;
  if (nothing_selected) cur_selected = 0;
  for(std::size_t i = 0; i < std::min(completion_items.size(), max_items) + 1; ++i)
  {
    std::size_t index = (cur_selected + i) % completion_items.size();
    if (completion_items[index].selected)
    {
      const auto& item = completion_items[index];
      draw_with_attr(p, *pmenu_sel, item, cur_y);
      if (!item.info.trimmed().isEmpty())
      {
        draw_info(p, *pmenu, item.info);
      }
      else
      {
        draw_info(p, *pmenu, QString());
      }
    }
    else
    {
      draw_with_attr(p, *pmenu, completion_items[index], cur_y);
    }
    cur_y += cell_height;
  }
  if (nothing_selected) cur_selected = -1;
  update();
}

void PopupMenu::font_changed(const QFont& font, float c_width, float c_height, int line_spacing)
{
  pmenu_font = font;
  // pmenu_font doesn't include linespacing. cell_width and cell_height give us that
  // information
  // c_width includes linespace
  cell_width = c_width;
  cell_height = c_height;
  linespace = line_spacing;
  QFontMetricsF fm {font};
  font_ascent = fm.ascent();
  icon_manager.size_changed(cell_height + icon_size_offset);
  update_dimensions();
}

void PopupMenu::draw_with_attr(QPainter& p, const HLAttr& attr, const PMenuItem& item, int y)
{
  float offset = font_ascent + (linespace / 2.f);
  const HLAttr& def_clrs = hl_state->default_colors_get();
  Color fg = attr.fg().value_or(*def_clrs.fg());
  Color bg = attr.bg().value_or(*def_clrs.bg());
  if (attr.reverse) std::swap(fg, bg);
  pmenu_font.setBold(attr.font_opts & FontOpts::Bold);
  pmenu_font.setItalic(attr.font_opts & FontOpts::Italic);
  pmenu_font.setUnderline(attr.font_opts & FontOpts::Underline);
  p.setFont(pmenu_font);
  const QPixmap* icon_ptr = icon_manager.icon_for_kind(item.kind);
  const QColor* icon_bg = icon_manager.bg_for_kind(item.kind);
  int start_x = std::ceil(border_width);
  // int end_y = y + cell_height;
  int end_x = pixmap.width() - border_width;
  p.setClipRect(QRect(start_x, y, end_x, cell_height));
  p.fillRect(QRect(start_x, y, end_x, cell_height), QColor(bg.r, bg.g, bg.b));
  QPoint text_start = {start_x, int(y + offset)};
  p.setPen(QColor(fg.r, fg.g, fg.b));
  if (icon_ptr && icon_bg && icons_enabled)
  {
    if (icons_on_right)
    {
      p.fillRect(QRect(start_x, y, cell_height, cell_height), *icon_bg);
      p.drawPixmap(
        {end_x - icon_ptr->width(), y - icon_size_offset / 2,
        icon_ptr->width(), icon_ptr->height()},
        *icon_ptr, icon_ptr->rect()
      );
      // Ensure text does not enter the pixmap's area
      int w = end_x - icon_ptr->width() - start_x;
      p.setClipRect(start_x, y, w, cell_height);
    }
    else
    {
      p.drawPixmap(
        {start_x, y - icon_size_offset / 2,
        icon_ptr->width(), icon_ptr->height()},
        *icon_ptr, icon_ptr->rect()
      );
      text_start.setX(text_start.x() + icon_ptr->width() * icon_space);
    }
  }
  p.drawText(text_start, item.word);
}

void PopupMenu::paintEvent(QPaintEvent* event)
{
  Q_UNUSED(event);
  QPainter p(this);
  p.drawPixmap(rect(), pixmap, rect());
  QPen pen {border_color};
  pen.setWidth(border_width);
  p.setPen(std::move(pen));
  QRect draw_rect = rect();
  int offset = std::ceil(float(border_width) / 2.f);
  draw_rect.adjust(0, 0, -offset, -offset);
  p.drawRect(draw_rect);
}

void PopupMenu::set_max_items(std::size_t new_max)
{
  max_items = new_max;
  update_dimensions();
}

void PopupMenu::set_max_chars(std::size_t new_max)
{
  max_chars = new_max;
  update_dimensions();
}

void PopupMenu::update_dimensions()
{
  pixmap = QPixmap(
    attached_width.value_or(max_chars * cell_width + border_width * 2),
    max_items * cell_height + border_width * 2
  );
  pixmap.fill(border_color);
  resize(available_rect().size());
}

void PopupMenu::draw_info(QPainter& p, const HLAttr& attr, const QString& info)
{
  Q_UNUSED(p); Q_UNUSED(attr); Q_UNUSED(info);
  //info_widget.draw(p, attr, info);
}
