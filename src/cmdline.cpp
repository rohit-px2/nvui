#include "cmdline.hpp"
#include <QPainter>
#include <QPaintEvent>
#include "msgpack_overrides.hpp"
#include <fmt/format.h>
#include <fmt/core.h>

static auto get_font_dimensions_for(const QFont& font)
{
  QFontMetricsF fm {font};
  return std::make_tuple(fm.averageCharWidth(), fm.height());
}

CmdLine::CmdLine(const HLState* hl_state, Cursor* cursor, QWidget* parent)
: QWidget(parent),
  state(hl_state),
  nvim_cursor(cursor),
  font(),
  big_font(),
  reg_metrics(font),
  big_metrics(big_font)
{
  // Using WinEditorArea, you won't get the opacity effect.
  // This is likely a consequence of bypassing Qt's rendering.
  auto* shadow_effect = new QGraphicsDropShadowEffect();
  shadow_effect->setBlurRadius(50.f);
  shadow_effect->setColor({0, 0, 0, 110});
  setGraphicsEffect(shadow_effect);
  setVisible(false);
  font.setPointSizeF(font_size);
  big_font.setPointSizeF(font_size * big_font_scale_ratio);
  update_metrics();
  setAttribute(Qt::WA_NativeWindow);
}

void CmdLine::font_changed(const QFont& new_font)
{
  font.setFamily(new_font.family());
  big_font.setFamily(new_font.family());
  font.setPointSizeF(font_size);
  big_font.setPointSizeF(font_size * big_font_scale_ratio);
  update_metrics();
}

void CmdLine::update_metrics()
{
  reg_metrics = QFontMetricsF(font);
  big_metrics = QFontMetricsF(big_font);
  std::tie(font_width, font_height) = get_font_dimensions_for(font);
  std::tie(big_font_width, big_font_height) = get_font_dimensions_for(big_font);
}

void CmdLine::cmdline_show(NvimObj obj, msg_size size)
{
  assert(obj);
  // Clear the previous text
  lines.clear();
  if (size < 1) return;
  const msgpack::object& o = obj[size - 1];
  assert(o.type == msgpack::type::ARRAY);
  const msgpack::object_array& arr = o.via.array;
  assert(arr.size == 6);
  assert(arr.ptr[0].type == msgpack::type::ARRAY);
  assert(arr.ptr[1].type == msgpack::type::POSITIVE_INTEGER);
  assert(arr.ptr[2].type == msgpack::type::STR);
  assert(arr.ptr[3].type == msgpack::type::STR);
  assert(arr.ptr[4].type == msgpack::type::POSITIVE_INTEGER);
  assert(arr.ptr[5].type == msgpack::type::POSITIVE_INTEGER);
  const auto& content = arr.ptr[0].via.array;
  add_line(content, lines);
  const int c_pos = arr.ptr[1].as<int>();
  cursor_pos = c_pos;
  QString firstc = arr.ptr[2].as<QString>();
  if (!firstc.isEmpty()) first_char = std::move(firstc);
  else first_char.reset();
  QString prompt = arr.ptr[3].as<QString>();
  // int indent = arr.ptr[4].as<int>();
  // int level = arr.ptr[5].as<int>();
  int new_height = padded(fitting_height());
  if (new_height != height()) resize(width(), new_height);
  update();
  setVisible(true);
}

void CmdLine::cmdline_hide(NvimObj obj, msg_size size)
{
  Q_UNUSED(obj);
  Q_UNUSED(size);
  setVisible(false);
}

void CmdLine::cmdline_cursor_pos(NvimObj obj, msg_size size)
{
  Q_UNUSED(size);
  assert(obj->type == msgpack::type::ARRAY);
  const auto& arr = obj->via.array;
  assert(arr.size == 2);
  assert(arr.ptr[0].type == msgpack::type::POSITIVE_INTEGER);
  assert(arr.ptr[1].type == msgpack::type::POSITIVE_INTEGER);
  const int pos = arr.ptr[0].as<int>();
  // const int level = arr.ptr[1].as<int>();
  cursor_pos = pos;
  update();
}

void CmdLine::cmdline_special_char(NvimObj, msg_size)
{
}

void CmdLine::cmdline_block_show(NvimObj, msg_size)
{
}

void CmdLine::cmdline_block_append(NvimObj, msg_size)
{
}

void CmdLine::cmdline_block_hide(NvimObj, msg_size)
{
}

static void draw_border(QPainter& p, QRect rect, float border_size)
{
  const int offset = std::ceil(border_size / 2.f);
  rect.adjust(offset / 2, offset / 2, -offset, -offset);
  p.drawRect(rect);
}

void CmdLine::paintEvent(QPaintEvent* event)
{
  const HLAttr& default_colors = state->default_colors_get();
  Q_UNUSED(event);
  QColor def_fg = inner_fg.value_or(default_colors.fg().value_or(0x00ffffff).qcolor());
  QColor def_bg = inner_bg.value_or(default_colors.bg().value_or(0).qcolor());
  QPainter p(this);
  p.fillRect(rect(), def_bg);
  QString thing;
  p.setPen(def_fg);
  int base_x = border_width + padding;
  int base_y = border_width + padding;
  struct {
    int x;
    int y;
  } pt = {base_x, base_y};
  const int big_offset = big_metrics.ascent();
  const int offset = reg_metrics.ascent();
  pt.y += first_char ? std::max(big_offset, offset) : offset;
  if (first_char)
  {
    p.setFont(big_font);
    p.drawText(QPoint {pt.x, pt.y}, first_char.value());
    pt.x += p.fontMetrics().horizontalAdvance(first_char.value());
  }
  p.setFont(font);
  int cur_char = 0;
  bool cursor_drawn = false;
  QRect inner = inner_rect();
  for(std::size_t i = 0; i < lines.size(); ++i)
  {
    for(const auto& seq : lines[i])
    {
      for(const QChar& c : seq.first)
      {
        int text_width = p.fontMetrics().horizontalAdvance(c);
        if (pt.x + text_width > inner.width())
        {
          pt.x = base_x;
          if (pt.y == base_y) pt.y += std::max(big_font_height, font_height);
          else pt.y += font_height;
        }
        p.drawText(QPoint {pt.x, pt.y}, c);
        if (cursor_pos && cur_char == cursor_pos.value())
        {
          auto c_rect_opt = nvim_cursor->rect(font_width, font_height);
          if (c_rect_opt)
          {
            auto&& c_rect = c_rect_opt.value();
            const HLAttr& a = state->attr_for_id(c_rect.hl_id);
            Color bg = a.bg().value_or(*default_colors.bg());
            Color fg = a.fg().value_or(*default_colors.fg());
            if (c_rect.hl_id == 0 || a.reverse) std::swap(fg, bg);
            QRect rect = c_rect.rect.toRect();
            int y = pt.y - offset;
            rect.moveTo({pt.x, y});
            p.fillRect(rect, QColor(bg.r, bg.g, bg.b));
            cursor_drawn = true;
          }
        }
        ++cur_char;
        pt.x += text_width;
      }
    }
    if (i != lines.size() - 1)
    {
      pt.y += pt.y == base_y 
        ? std::max(big_font_height, font_height)
        : font_height;
    }
  }
  if (cursor_pos && !cursor_drawn)
  {
    auto c_rect_opt = nvim_cursor->rect(font_width, font_height);
    if (c_rect_opt)
    {
      auto&& c_rect = c_rect_opt.value();
      const HLAttr& a = state->attr_for_id(c_rect.hl_id);
      Color bg = a.bg().value_or(*default_colors.bg());
      Color fg = a.fg().value_or(*default_colors.fg());
      if (c_rect.hl_id == 0 || a.reverse) std::swap(fg, bg);
      if (pt.y == base_y) pt.y -= big_offset;
      else pt.y -= offset;
      QRect rect = c_rect.rect.toRect();
      rect.moveTo({pt.x, pt.y});
      p.fillRect(rect, QColor(bg.r, bg.g, bg.b));
      cursor_drawn = true;
    }
  }
  if (border_width == 0.f) return;
  QPen pen {border_color, border_width};
  p.setPen(pen);
  draw_border(p, rect(), border_width);
}

void CmdLine::draw_text_and_bg(
  QPainter& painter,
  const QString& text,
  const HLAttr& attr,
  const HLAttr& def_clrs,
  const QPointF& start,
  const QPointF& end,
  const int offset
)
{
  font::set_opts(font, attr.font_opts);
  painter.setFont(font);
  Color fg = attr.fg().value_or(*def_clrs.fg());
  Color bg = attr.bg().value_or(*def_clrs.bg());
  if (attr.reverse) std::swap(fg, bg);
  const QRectF rect = {start, end};
  painter.setClipRect(rect);
  painter.fillRect(rect, QColor(bg.r, bg.g, bg.b));
  painter.setPen(QColor(fg.r, fg.g, fg.b));
  const QPointF text_start = {start.x(), start.y() + offset};
  painter.drawText(text_start, text);
}

void CmdLine::add_line(
  const msgpack::object_array& new_line,
  std::vector<line>& line_arr
)
{
  line line;
  for(msg_size i = 0; i < new_line.size; ++i)
  {
    assert(new_line.ptr[i].type == msgpack::type::ARRAY);
    const auto& arr = new_line.ptr[i].via.array;
    assert(arr.size == 2);
    assert(arr.ptr[0].type == msgpack::type::POSITIVE_INTEGER);
    assert(arr.ptr[1].type == msgpack::type::STR);
    int hl_id = arr.ptr[0].as<int>();
    QString text = arr.ptr[1].as<QString>();
    line.push_back({std::move(text), hl_id});
  }
  line_arr.push_back(std::move(line));
}


int CmdLine::fitting_height()
{
  if (lines.size() == 0) return big_font_height;
  int height = 0;
  QPainter p(&pixmap);
  p.setFont(big_font);
  int max_width = inner_rect().width();
  const int base_x = border_width + padding;
  for(std::size_t i = 0; i < lines.size(); ++i)
  {
    int width = base_x;
    if (i == 0 && first_char)
    {
      width += p.fontMetrics().horizontalAdvance(first_char.value());
      p.setFont(font);
    }
    int num_lines = 1;
    for(const auto& seq : lines[i])
    {
      for(const QChar& c : seq.first)
      {
        int text_width = p.fontMetrics().horizontalAdvance(c);
        if (width + text_width > max_width)
        {
          width = base_x;
          ++num_lines;
        }
        width += text_width;
      }
    }
    int height_for_line = num_lines * font_height;
    if (i == 0 && first_char && big_font_height > font_height)
    {
      height_for_line += (big_font_height - font_height);
    }
    height += height_for_line;
  }
  return height;
}
