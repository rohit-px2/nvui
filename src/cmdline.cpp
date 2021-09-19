#include "cmdline.hpp"
#include <QPainter>
#include <QPaintEvent>
#include "msgpack_overrides.hpp"
#include <fmt/format.h>
#include <fmt/core.h>

static auto get_font_dimensions_for(const QFont& font)
{
  QFontMetrics fm {font};
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
  reg_metrics = QFontMetrics(font);
  big_metrics = QFontMetrics(big_font);
  std::tie(font_width, font_height) = get_font_dimensions_for(font);
  std::tie(big_font_width, big_font_height) = get_font_dimensions_for(big_font);
}

void CmdLine::cmdline_show(std::span<Object> objs)
{
  // Clear the previous text
  lines.clear();
  if (objs.empty()) return;
  auto& obj = objs.back();
  auto* arr = obj.array();
  assert(arr && arr->size() >= 6);
  if (!arr->at(0).has<ObjectArray>()) return;
  ObjectArray& content = *arr->at(0).array();
  add_line(content);
  auto* c_pos = arr->at(1).u64();
  auto* firstc = arr->at(2).string();
  //auto* prompt = arr->at(3).string();
  //auto* indent = arr->at(4).u64();
  //auto* level = arr->at(5).u64();
  if (!c_pos || !firstc) return;
  cursor_pos = static_cast<int>(*c_pos);
  if (!firstc->isEmpty()) first_char = std::move(*firstc);
  else first_char.reset();
  // int indent = arr.ptr[4].as<int>();
  // int level = arr.ptr[5].as<int>();
  int new_height = padded(fitting_height());
  if (new_height != height()) resize(width(), new_height);
  update();
  setVisible(true);
}

void CmdLine::cmdline_hide(std::span<Object> objs)
{
  Q_UNUSED(objs);
  setVisible(false);
}

void CmdLine::cmdline_cursor_pos(std::span<Object> objs)
{
  for(auto& obj : objs)
  {
    auto* arr = obj.array();
    assert(arr && arr->size() >= 2);
    assert(arr->at(0).u64() && arr->at(1).u64());
    cursor_pos = arr->at(0);
    // const auto level = arr->at(1).get<u64>();
  }
  update();
}

void CmdLine::cmdline_special_char(std::span<Object>)
{
}

void CmdLine::cmdline_block_show(std::span<Object>)
{
}

void CmdLine::cmdline_block_append(std::span<Object>)
{
}

void CmdLine::cmdline_block_hide(std::span<Object>)
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
  Q_UNUSED(event);
  QPainter p(this);
  p.fillRect(rect(), inner_bg);
  QString thing;
  p.setPen(inner_fg);
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
  const QFontMetrics& font_metrics = p.fontMetrics();
  int cur_char = 0;
  bool cursor_drawn = false;
  QRect inner = inner_rect();
  const HLAttr& default_colors = state->default_colors_get();
  for(std::size_t i = 0; i < lines.size(); ++i)
  {
    for(const auto& seq : lines[i])
    {
      for(const QChar& c : seq.first)
      {
        int text_width = font_metrics.horizontalAdvance(c);
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
  font.setItalic(attr.font_opts & FontOpts::Italic);
  font.setBold(attr.font_opts & FontOpts::Bold);
  font.setUnderline(attr.font_opts & FontOpts::Underline);
  font.setStrikeOut(attr.font_opts & FontOpts::Strikethrough);
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

void CmdLine::add_line(ObjectArray& new_line)
{
  line line;
  for(auto& line_obj : new_line)
  {
    assert(line_obj.has<ObjectArray>());
    auto& arr = line_obj.get<ObjectArray>();
    assert(arr.size() == 2);
    int hl_id = arr.at(0).get_as<int>().value_or(0);
    QString& text = arr.at(1).get<QString>();
    line.push_back({std::move(text), hl_id});
  }
  lines.push_back(std::move(line));
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
