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
  const auto& line = arr->at(0).get<ObjectArray>();
  convert_content(content, line);
  cursor_pos = arr->at(1).try_convert<int>().value_or(0);
  auto* firstc = arr->at(2).string();
  if (firstc && firstc->empty()) first_char.reset();
  else first_char = QString::fromStdString(*firstc);
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
}

void Cmdline::cmdline_block_append(std::span<const Object> objs)
{
  Q_UNUSED(objs);
}

void Cmdline::cmdline_block_hide(std::span<const Object> objs)
{
  Q_UNUSED(objs);
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

int CmdlineQ::fitting_height() const
{
  int maxwidth = width() - (border_width + padding) * 2;
  auto contentstring = get_content_string();
  QFontMetricsF fm {cmd_font};
  auto maxheight = fm.height() * contentstring.size();
  QRectF constraint(0, 0, maxwidth, maxheight);
  auto bound = fm.boundingRect(constraint, Qt::TextWrapAnywhere, contentstring);
  return bound.height() + (2 * (border_width + padding));
}

void CmdlineQ::draw_cursor(QPainter& p, const Cursor& cursor)
{
  if (cursor.hidden()) return;
  if (!cursor.pos()) return;
  float pad = border_width + padding;
  QFontMetricsF fm {cmd_font};
  float left = pad;
  float top = pad;
  const auto incx = [&](float adv) {
    if (left + adv > (width() - pad)) {
      left = pad;
      top += fm.height();
    }
    left += adv;
  };
  for(const auto& line : block) {
    for(const auto& chunk : line) {
      for(const auto& c : chunk.text) {
        incx(fm.horizontalAdvance(c));
      }
    }
    top += fm.height();
  }
  QString ctn;
  if (first_char) { ctn += first_char.value(); ++cursor_pos; }
  for(const auto& chunk : content) ctn += chunk.text;
  for(int i = 0; i < cursor_pos && i < ctn.size(); ++i) {
    incx(fm.horizontalAdvance(ctn.at(i)));
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
  QPainter p(this);
  p.setFont(cmd_font);
  auto contentstring = get_content_string();
  QColor bg = inner_bg.value_or(hl_state.default_bg()).qcolor();
  QColor fg = inner_fg.value_or(hl_state.default_fg()).qcolor();
  p.fillRect(rect(), bg);
  p.setPen(fg);
  int pad = border_width + padding;
  QRect bounding_rect = {pad, pad, width() - pad * 2, height() - pad * 2};
  p.drawText(bounding_rect, Qt::TextWrapAnywhere, contentstring);
  if (border_width > 0.f)
  {
    QPen pen;
    pen.setWidthF(border_width);
    pen.setColor(border_color.qcolor());
    p.setPen(pen);
    float mid = border_width / 2.f;
    QRectF b_rect(QPointF {mid, mid}, QPointF {width() - mid, height() - mid});
    p.drawRect(b_rect);
  }
  if (p_cursor) draw_cursor(p, *p_cursor);
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

void CmdLine::cmdline_show(std::span<const Object> objs)
{
  // Clear the previous text
  lines.clear();
  if (objs.empty()) return;
  auto& obj = objs.back();
  auto* arr = obj.array();
  assert(arr && arr->size() >= 6);
  if (!arr->at(0).has<ObjectArray>()) return;
  const ObjectArray& content = *arr->at(0).array();
  add_line(content);
  auto* c_pos = arr->at(1).u64();
  auto* firstc = arr->at(2).string();
  //auto* prompt = arr->at(3).string();
  //auto* indent = arr->at(4).u64();
  //auto* level = arr->at(5).u64();
  if (!c_pos || !firstc) return;
  cursor_pos = static_cast<int>(*c_pos);
  if (!firstc->empty()) first_char = QString::fromStdString(*firstc);
  else first_char.reset();
  // int indent = arr.ptr[4].as<int>();
  // int level = arr.ptr[5].as<int>();
  int new_height = padded(fitting_height());
  if (new_height != height()) resize(width(), new_height);
  update();
  setVisible(true);
}

void CmdLine::cmdline_hide(std::span<const Object>)
{
  setVisible(false);
}

void CmdLine::cmdline_cursor_pos(std::span<const Object> objs)
{
  for(auto& obj : objs)
  {
    auto* arr = obj.array();
    assert(arr && arr->size() >= 2);
    assert(arr->at(0).u64() && arr->at(1).u64());
    cursor_pos = (int) arr->at(0);
    // const auto level = arr->at(1).get<u64>();
  }
  update();
}

void CmdLine::cmdline_special_char(std::span<const Object>)
{
}

void CmdLine::cmdline_block_show(std::span<const Object>)
{
}

void CmdLine::cmdline_block_append(std::span<const Object>)
{
}

void CmdLine::cmdline_block_hide(std::span<const Object>)
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
  const QFontMetrics& font_metrics = p.fontMetrics();
  int cur_char = 0;
  bool cursor_drawn = false;
  QRect inner = inner_rect();
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
          auto c_rect_opt = nvim_cursor->rect(font_width, font_height, 1.0f, false);
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
    auto c_rect_opt = nvim_cursor->rect(font_width, font_height, 1.0f, false);
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

void CmdLine::add_line(const ObjectArray& new_line)
{
  line line;
  for(auto& line_obj : new_line)
  {
    assert(line_obj.has<ObjectArray>());
    auto& arr = line_obj.get<ObjectArray>();
    assert(arr.size() == 2);
    int hl_id = arr.at(0).try_convert<int>().value_or(0);
    const QString& text = QString::fromStdString(arr.at(1).get<std::string>());
    line.emplace_back(text, hl_id);
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
