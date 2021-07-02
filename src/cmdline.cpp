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

CmdLine::CmdLine(const HLState* hl_state, int width, int height, QWidget* parent)
: QWidget(parent),
  state(hl_state),
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
  setAttribute(Qt::WA_NativeWindow);
}

void CmdLine::font_changed(const QFont& new_font)
{
  font = new_font;
  big_font = new_font;
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
  assert(arr.ptr[3].type == mspgack::type::STR);
  assert(arr.ptr[4].type == msgpack::type::POSITIVE_INTEGER);
  assert(arr.ptr[5].type == msgpack::type::POSITIVE_INTEGER);
  const auto& content = arr.ptr[0].via.array;
  add_line(content);
  const int cursor_pos = arr.ptr[1].as<int>();
  QString firstc = arr.ptr[2].as<QString>();
  if (!firstc.isEmpty()) first_char = std::move(firstc);
  else first_char.reset();
  QString prompt = arr.ptr[3].as<QString>();
  int indent = arr.ptr[4].as<int>();
  int level = arr.ptr[5].as<int>();
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
}

void CmdLine::cmdline_special_char(NvimObj obj, msg_size size)
{
  std::cout << *obj << '\n';
}

void CmdLine::cmdline_block_show(NvimObj obj, msg_size size)
{
  std::cout << "Block show: " << *obj << '\n';
}

void CmdLine::cmdline_block_append(NvimObj obj, msg_size size)
{
  std::cout << "Block append: " << *obj << '\n';
}

void CmdLine::cmdline_block_hide(NvimObj obj, msg_size size)
{
  std::cout << "block_hide: " << *obj << '\n';
}

static void draw_border(QPainter& p, QRect rect, float border_size)
{
  const int offset = std::ceil(border_size / 2.f);
  rect.adjust(offset / 2.f, offset / 2.f, -offset, -offset);
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
  pt.y += first_char ? big_metrics.ascent() : reg_metrics.ascent();
  if (first_char)
  {
    p.setFont(big_font);
    p.drawText(QPoint {pt.x, pt.y}, first_char.value());
    pt.x += big_metrics.horizontalAdvance(first_char.value());
  }
  p.setFont(font);
  QRect inner = inner_rect();
  for(const auto& line : lines)
  {
    for(const auto& seq : line)
    {
      for(const QChar& c : seq.first)
      {
        int text_width = reg_metrics.horizontalAdvance(c);
        if (pt.x + text_width > inner.width())
        {
          pt.x = base_x;
          if (pt.y == base_y) pt.y += std::max(big_font_height, font_height);
          else pt.y += font_height;
        }
        p.drawText(QPoint {pt.x, pt.y}, c);
        pt.x += text_width;
      }
    }
    pt.y += pt.y == base_y ? std::max(big_font_height, font_height) : font_height;
  }
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
  Color fg = attr.has_fg ? attr.foreground : def_clrs.foreground;
  Color bg = attr.has_bg ? attr.background : def_clrs.background;
  if (attr.reverse) std::swap(fg, bg);
  const QRectF rect = {start, end};
  painter.setClipRect(rect);
  painter.fillRect(rect, QColor(bg.r, bg.g, bg.b));
  painter.setPen(QColor(fg.r, fg.g, fg.b));
  const QPointF text_start = {start.x(), start.y() + offset};
  painter.drawText(text_start, text);
}

void CmdLine::add_line(const msgpack::object_array& new_line)
{
  line line;
  for(msg_size i = 0; i < new_line.size; ++i)
  {
    assert(new_line.ptr[i].type == msgpack::type::ARRAY);
    const auto& arr = new_line.ptr[i].via.array;
    assert(arr.size == 2);
    assert(arr.ptr[0].type == msgpack::type::POSITIVE_INTEGER);
    assert(arr.ptr[1].type == msgpack::type::STRING);
    int hl_id = arr.ptr[0].as<int>();
    QString text = arr.ptr[1].as<QString>();
    line.push_back({std::move(text), hl_id});
  }
  lines.push_back(std::move(line));
}
