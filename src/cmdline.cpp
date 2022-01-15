#include "cmdline.hpp"
#include <QPainter>
#include <QPaintEvent>
#include <fmt/format.h>
#include <fmt/core.h>

Cmdline::Cmdline(const HLState& hl, const Cursor* crsr)
  : hl_state(hl), p_cursor(crsr)
{
}

void Cmdline::cmdline_show(std::span<const Object> objs)
{
  is_hidden = false;
  content.clear();
  if (objs.empty()) return;
  auto& back = objs.back();
  auto* arr = back.array();
  if (!(arr && arr->size() >= 6)) return;
  if (!arr->at(0).is_array()) return;
  const auto& line = arr->at(0).array_ref();
  convert_content(content, line);
  cursor_pos = arr->at(1).try_convert<int>().value_or(0);
  auto* firstc = arr->at(2).string();
  if (firstc && firstc->empty()) first_char.reset();
  else first_char = QString::fromStdString(*firstc);
  indent = arr->at(4).try_convert<int>().value_or(0);
  update_content_string();
  redraw();
  do_show();
}

void Cmdline::cmdline_hide(std::span<const Object>)
{
  is_hidden = true;
  do_hide();
}

bool Cmdline::hidden() const { return is_hidden; }

void Cmdline::cmdline_cursor_pos(std::span<const Object> objs)
{
  for(auto& obj : objs)
  {
    auto* arr = obj.array();
    if (!(arr && arr->size() >= 2)) continue;
    cursor_pos = arr->at(0).try_convert<int>().value_or(0);
    // const auto level = arr->at(1).get<u64>();
  }
  redraw();
}

void Cmdline::set_fg(Color fg)
{
  inner_fg = fg;
  colors_changed(fg, inner_bg.value_or(hl_state.default_bg()));
}

void Cmdline::set_bg(Color bg)
{
  inner_bg = bg;
  colors_changed(inner_fg.value_or(hl_state.default_fg()), bg);
}

void Cmdline::cmdline_special_char(std::span<const Object>)
{
}

void Cmdline::set_border_color(Color color)
{
  border_color = color;
  border_changed();
}

void Cmdline::set_border_width(int pixels)
{
  border_width = (float) pixels;
  border_changed();
}

void Cmdline::set_x(float left)
{
  if (left <= 0.f || left > 1.0f) return;
  centered_x.reset();
  rel_rect.setX(left);
}

void Cmdline::set_y(float top)
{
  if (top <= 0.f || top > 1.0f) return;
  centered_y.reset();
  rel_rect.setY(top);
}

void Cmdline::set_center_x(float x)
{
  if (x <= 0.f || x > 1.0f) return;
  float cur_width = rel_rect.width();
  rel_rect.setX(x - (cur_width / 2.f));
  centered_x = x;
  rect_changed(rel_rect);
}

void Cmdline::set_center_y(float y)
{
  if (y <= 0.f || y > 1.0f) return;
  float cur_height = rel_rect.height();
  rel_rect.setY(y - (cur_height / 2.f));
  centered_y = y;
  rect_changed(rel_rect);
}

void Cmdline::set_width(float w)
{
  if (w <= 0.f || w > 1.0f) return;
  rel_rect.setWidth(w);
  rect_changed(rel_rect);
}

void Cmdline::set_height(float h)
{
  if (h <= 0.f || h > 1.0f) return;
  rel_rect.setHeight(h);
  rect_changed(rel_rect);
}

void Cmdline::convert_content(Content& cont, const ObjectArray& obj)
{
  for(const auto& chunk : obj)
  {
    auto* arr = chunk.array();
    if (!(arr && arr->size() >= 2)) continue;
    auto vars = chunk.try_decompose<int, std::string>();
    if (!vars) continue;
    const auto& [attr, string] = *vars;
    cont.push_back(Chunk {attr, QString::fromStdString(string)});
  }
}

void Cmdline::update_content_string()
{
  auto& s = complete_content_string;
  s.clear();
  if (first_char) s.append(first_char.value());
  for(const auto& line : block)
  {
    for(const auto& [_, text] : line) s.append(text);
    s.append('\n');
  }
  s.append(QString(indent, QChar(' ')));
  for(const auto& [_, text] : content) s.append(text);
}

QString Cmdline::get_content_string() const
{
  return complete_content_string;
}

void Cmdline::set_padding(u32 pad)
{
  padding = pad;
}

void Cmdline::cmdline_block_show(std::span<const Object> objs)
{
  Q_UNUSED(objs);
  block.clear();
  if (objs.empty()) return;
  if (!objs.back().is_array()) return;
  const auto& linesarr = objs.back().array_ref();
  for(const auto& lines : linesarr)
  {
    if (!lines.is_array()) continue;
    const auto& linearr = lines.array_ref();
    for(const auto& line : linearr)
    {
      if (!line.is_array()) continue;
      const auto& to_convert = line.get<ObjectArray>();
      convert_content(block.emplace_back(), to_convert);
    }
  }
  redraw();
  do_show();
}

void Cmdline::cmdline_block_append(std::span<const Object> objs)
{
  Q_UNUSED(objs);
  if (objs.empty()) return;
  const auto& line = objs.back().try_at(0);
  if (line.is_array())
  {
    convert_content(block.emplace_back(), line.get<ObjectArray>());
  }
  redraw();
}

void Cmdline::cmdline_block_hide(std::span<const Object> objs)
{
  Q_UNUSED(objs);
  block.clear();
  redraw();
}

CmdlineQ::CmdlineQ(const HLState& hl_state, const Cursor* crs, QWidget* parent)
: Cmdline(hl_state, crs), QWidget(parent)
{
  hide();
  cmd_font.setPointSizeF(14.0);
  cmd_font.setFamily(default_font_family());
}

CmdlineQ::~CmdlineQ() = default;

void CmdlineQ::register_nvim(Nvim&)
{
}

void CmdlineQ::colors_changed(Color, Color)
{
}

void CmdlineQ::do_show()
{
  show();
}

void CmdlineQ::do_hide()
{
  hide();
}

void CmdlineQ::redraw()
{
  resize(width(), fitting_height());
  update();
}

QRect CmdlineQ::get_rect() const
{
  return {x(), y(), width(), height()};
}

void CmdlineQ::set_font_family(std::string_view family)
{
  cmd_font.setFamily(QString::fromUtf8(family.data(), (int) family.size()));
}

void CmdlineQ::set_font_size(double point_size)
{
  cmd_font.setPointSizeF(point_size);
}

void CmdlineQ::editor_resized(int, int)
{
  rect_changed(rel_rect);
}

void CmdlineQ::border_changed()
{
}

int CmdlineQ::num_lines(const Content& content, const QFontMetricsF& fm) const
{
  double maxwidth = width() - (border_width + padding) * 2;
  double w = 0;
  for(const auto& [_, text] : content) w += fm.horizontalAdvance(text);
  return std::max(std::ceil(w / maxwidth), 1.);
}

void CmdlineQ::rect_changed(QRectF relative_rect)
{
  auto* parent = parentWidget();
  if (parent)
  {
    auto size = parent->size();
    auto x = relative_rect.x() * size.width();
    auto y = relative_rect.y() * size.height();
    auto w = relative_rect.width() * size.width();
    auto h = relative_rect.height() * size.height();
    if (w != width() || h != height()) resize(w, h);
    if (x != pos().x() || y != pos().y()) move(x, y);
  }
}

static void incx(
  float& left, float& top, float adv, float maxwidth, float pad, float height
)
{
  if (left + adv > maxwidth - pad)
  {
    left = pad;
    top += height;
  }
  left += adv;
}

static std::tuple<float, float> draw_pos(
  float left,
  float top,
  float adv,
  float maxwidth,
  float pad,
  float height
)
{
  if (left + adv > maxwidth - pad)
  {
    return {pad, top + height};
  }
  return {left, top};
}

int CmdlineQ::fitting_height() const
{
  int maxwidth = width() - (border_width + padding) * 2;
  auto contentstring = get_content_string();
  QFontMetricsF fm {cmd_font};
  auto maxheight = fm.height() * contentstring.size();
  QRectF constraint(0, 0, maxwidth, maxheight);
  float pad = border_width + padding;
  float left = pad;
  float top = pad;
  const auto adv_x = [&](float adv) {
    incx(left, top, adv, width(), pad, fm.height());
  };
  for(const auto& c : contentstring)
  {
    if (c == '\n') { left = pad; top += fm.height(); }
    else adv_x(fm.horizontalAdvance(c));
  }
  return top + fm.height() + pad;
}

void CmdlineQ::draw_cursor(QPainter& p, const Cursor& cursor)
{
  if (cursor.hidden()) return;
  if (!cursor.pos()) return;
  int cur_content_length = 0;
  for(const auto& chunk : content) cur_content_length += chunk.text.size();
  QString contentstring = get_content_string();
  int upto = contentstring.size() - cur_content_length + cursor_pos;
  float pad = border_width + padding;
  QFontMetricsF fm {cmd_font};
  float left = pad;
  float top = pad;
  const auto adv_x = [&](float adv) {
    incx(left, top, adv, width(), pad, fm.height());
  };
  for(int i = 0; i < upto && i < contentstring.size(); ++i)
  {
    if (contentstring[i] == '\n') { left = pad; top += fm.height(); }
    else
    {
      adv_x(fm.horizontalAdvance(contentstring[i]));
    }
  }
  auto [rect, id, drawtext, opacity] = cursor.rect(
    fm.averageCharWidth(), fm.height(), 1.0f, false
  ).value();
  rect.moveTo(left, top);
  auto [fg, bg] = hl_state.colors_for(hl_state.attr_for_id(id));
  if (id == 0) std::swap(fg, bg);
  p.fillRect(rect, bg.qcolor());
}

void CmdlineQ::paintEvent(QPaintEvent*)
{
  QFontMetricsF fm {cmd_font};
  int offset = fm.ascent();
  QPainter p(this);
  p.setFont(cmd_font);
  auto contentstring = get_content_string();
  QColor bg = inner_bg.value_or(hl_state.default_bg()).qcolor();
  QColor fg = inner_fg.value_or(hl_state.default_fg()).qcolor();
  if (border_width > 0.f)
  {
    p.fillRect(rect(), border_color.qcolor());
  }
  p.setPen(fg);
  int border = border_width;
  QRect fill_rect(border, border, width() - 2 * border, height() - 2 * border);
  p.fillRect(fill_rect, bg);
  int pad = border_width + padding;
  float left = pad;
  float top = pad;
  for(const auto& c : contentstring)
  {
    if (c == '\n') { left = pad; top += fm.height(); }
    else
    {
      auto adv = fm.horizontalAdvance(c);
      auto [x, y] = draw_pos(left, top, adv, width(), pad, fm.height());
      p.drawText(QPointF(x, y + offset), c);
      incx(left, top, adv, width(), pad, fm.height());
    }
  }
  if (p_cursor) draw_cursor(p, *p_cursor);
}

