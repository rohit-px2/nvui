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
  const std::unordered_map<QString, fg_bg>& map,
  const QString& key,
  const QColor& default_fg,
  const QColor& default_bg
)
{
  const auto it = map.find(key);
  if (it != map.end()) {
    return {it->second.first.value_or(default_fg), it->second.second.value_or(default_bg)};
  }
  else return {default_fg, default_bg};
}

QPixmap PopupMenuIconManager::load_icon(const QString& iname, int width)
{
  auto&& [fg, bg] = find_or_default(colors, iname, default_fg, default_bg);
  auto&& px = pixmap_from_svg(constants::picon_fp() % iname % ".svg", fg, bg, width, width);
  return px.value_or(QPixmap());
}

void PopupMenuIconManager::load_icons(int width)
{
  for(auto& e : icons)
  {
    e.second = load_icon(e.first, width);
  }
}

const QPixmap* PopupMenuIconManager::icon_for_kind(const QString& kind)
{
  static const auto not_found = [this] {
    return nullptr;
  };
  if (kind.isEmpty()) return not_found();
  QString iname = kind_to_iname(kind).trimmed();
  const auto it = icons.find(iname);
  if (it == icons.end())
  {
    // Check the icons folder for the icon file
    QPixmap p = load_icon(iname, sq_width);
    if (p.isNull()) return not_found();
    else
    {
      icons[iname] = std::move(p);
      return &icons[iname];
    }
  }
  else return &it->second;
}

PopupMenuInfo::PopupMenuInfo(PopupMenu* parent)
  : QWidget(parent->parentWidget()),
    parent_menu(parent)
{
  hide();
}

void PopupMenuInfo::draw(QPainter& p, const HLAttr& attr, const QString& info)
{
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
  auto fg = current_attr.fg().value_or(0).qcolor();
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

void PopupMenu::pum_show(const msgpack::object* obj, std::uint32_t size)
{
  is_hidden = false;
  completion_items.clear();
  using std::tuple;
  using std::vector;
  using std::string;
  for(std::uint32_t i = 0; i < size; ++i)
  {
    const msgpack::object& o = obj[i];
    assert(o.type == msgpack::type::ARRAY);
    const msgpack::object_array& arr = o.via.array;
    assert(arr.size == 5);
    assert(arr.ptr[0].type == msgpack::type::ARRAY);
    const msgpack::object_array& items = arr.ptr[0].via.array;
    add_items(items);
    cur_selected = arr.ptr[1].as<int>();
    row = arr.ptr[2].as<int>();
    col = arr.ptr[3].as<int>();
    grid_num = arr.ptr[4].as<int>();
    if (cur_selected >= 0 && cur_selected < int(completion_items.size()))
    {
      completion_items[i].selected = true;
    }
    paint();
  }
  resize(available_rect().size());
}

void PopupMenu::pum_sel(const msgpack::object* obj, std::uint32_t size)
{
  assert(obj->type == msgpack::type::ARRAY);
  const msgpack::object_array& arr = obj->via.array;
  assert(arr.size == 1);
  if (cur_selected >= 0) completion_items[cur_selected].selected = false;
  cur_selected = arr.ptr[0].as<int>();
  if (cur_selected >= 0) completion_items[cur_selected].selected = true;
  paint();
}

void PopupMenu::pum_hide(const msgpack::object* obj, std::uint32_t size)
{
  Q_UNUSED(obj);
  Q_UNUSED(size);
  is_hidden = true;
  setVisible(false);
  info_widget.hide();
}

void PopupMenu::add_items(const msgpack::object_array& items)
{
  for(std::uint32_t i = 0; i < items.size; ++i)
  {
    const msgpack::object& item = items.ptr[i];
    assert(item.type == msgpack::type::ARRAY);
    const msgpack::object_array& wkmi = item.via.array;
    assert(wkmi.size == 4);
    assert(wkmi.ptr[0].type == msgpack::type::STR);
    assert(wkmi.ptr[1].type == msgpack::type::STR);
    assert(wkmi.ptr[2].type == msgpack::type::STR);
    assert(wkmi.ptr[3].type == msgpack::type::STR);
    QString word = wkmi.ptr[0].as<QString>();
    QString kind = wkmi.ptr[1].as<QString>();
    QString menu = wkmi.ptr[2].as<QString>();
    QString info = wkmi.ptr[3].as<QString>();
    completion_items.push_back({
      false,
      std::move(word), std::move(kind),
      std::move(menu),std::move(info)
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
  QFontMetrics fm {font};
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
  int start_x = std::ceil(border_width);
  int end_y = y + cell_height;
  int end_x = pixmap.width() - border_width;
  p.setClipRect(QRect(start_x, y, end_x, cell_height));
  p.fillRect(QRect(start_x, y, end_x, cell_height), QColor(bg.r, bg.g, bg.b));
  QPoint text_start = {start_x, int(y + offset)};
  p.setPen(QColor(fg.r, fg.g, fg.b));
  if (icon_ptr && icons_enabled)
  {
    if (icons_on_right)
    {
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
  if (cur_selected == -1) info_widget.hide();
  else info_widget.show();
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
  info_widget.draw(p, attr, info);
}
