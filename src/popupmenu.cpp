#include "popupmenu.hpp"
#include <iostream>
#include <QPainter>
#include "msgpack_overrides.hpp"

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
}

void PopupMenu::add_items(const msgpack::object_array& items)
{
  for(std::uint32_t i = 0; i < items.size; ++i)
  {
    const msgpack::object& item = items.ptr[i];
    assert(item.type == msgpack::type::ARRAY);
    const msgpack::object_array& wkmi = item.via.array;
    assert(wkmi.size == 4);
    assert(wkmi.ptr[0].type == mspgack::type::STR);
    assert(wkmi.ptr[1].type == mspgack::type::STR);
    assert(wkmi.ptr[2].type == mspgack::type::STR);
    assert(wkmi.ptr[3].type == mspgack::type::STR);
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
  for(std::size_t i = 0; i < completion_items.size(); ++i)
  {
    std::size_t index = (cur_selected + i) % completion_items.size();
    if (completion_items[index].selected)
    {
      draw_with_attr(p, *pmenu_sel, completion_items[index], cur_y);
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
  update_dimensions();
}

void PopupMenu::draw_with_attr(QPainter& p, const HLAttr& attr, const PMenuItem& item, int y)
{
  float offset = font_ascent + (linespace / 2.f);
  const HLAttr& def_clrs = hl_state->default_colors_get();
  Color fg = attr.has_fg ? attr.foreground : def_clrs.foreground;
  Color bg = attr.has_bg ? attr.background : def_clrs.background;
  if (attr.reverse) std::swap(fg, bg);
  pmenu_font.setBold(attr.font_opts & FontOpts::Bold);
  pmenu_font.setWeight(QFont::Medium);
  pmenu_font.setItalic(attr.font_opts & FontOpts::Italic);
  pmenu_font.setUnderline(attr.font_opts & FontOpts::Underline);
  p.setFont(pmenu_font);
  int start_x = std::ceil(border_width);
  int end_y = y + cell_height;
  int end_x = pixmap.width() - border_width;
  p.setClipRect(QRect(start_x, y, end_x, cell_height));
  p.fillRect(QRect(start_x, y, end_x, cell_height), QColor(bg.r, bg.g, bg.b));
  QPoint text_start = {start_x, int(y + offset)};
  p.setPen(QColor(fg.r, fg.g, fg.b));
  p.drawText(text_start, item.word);
}

void PopupMenu::paintEvent(QPaintEvent* event)
{
  Q_UNUSED(event);
  QPainter p(this);
  p.drawPixmap(rect(), pixmap, rect());
  QPen pen {QColor(0, 0, 0)};
  pen.setWidth(border_width);
  p.setPen(std::move(pen));
  QRect draw_rect = rect();
  draw_rect.setRight(draw_rect.right() - border_width);
  draw_rect.setBottom(draw_rect.bottom() - border_width);
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
  pixmap = QPixmap(max_chars * cell_width + border_width * 2, max_items * cell_height + border_width * 2);
  pixmap.fill(QColor(0, 0, 0));
  resize(available_rect().size());
}
