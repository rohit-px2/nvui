#include "editor.hpp"
#include "grid.hpp"
#include "utils.hpp"

void QPaintGrid::update_pixmap_size()
{
  auto&& [font_width, font_height] = editor_area->font_dimensions();
  pixmap = QPixmap(cols * font_width, rows * font_height);
  send_redraw();
}

void QPaintGrid::set_size(u16 w, u16 h)
{
  GridBase::set_size(w, h);
  update_pixmap_size();
}

void QPaintGrid::set_pos(u16 new_x, u16 new_y)
{
  if (!editor_area->animations_enabled())
  {
    GridBase::set_pos(new_x, new_y);
    update_position(new_x, new_y);
    return;
  }
  auto x_diff = new_x - x;
  auto y_diff = new_y - y;
  auto old_x = x, old_y = y;
  auto interval = editor_area->animation_frametime();
  move_animation_time = editor_area->move_animation_duration();
  move_update_timer.setInterval(interval);
  move_update_timer.callOnTimeout([=] {
    auto ms_interval = move_update_timer.interval();
    move_animation_time -= float(ms_interval) / 1000.f;
    if (move_animation_time <= 0)
    {
      move_update_timer.stop();
      GridBase::set_pos(new_x, new_y);
      update_position(new_x, new_y);
    }
    else
    {
      auto duration = editor_area->move_animation_duration();
      auto animation_left = move_animation_time / duration;
      float animation_finished = 1.0f - animation_left;
      float animated_x = old_x + (float(x_diff) * animation_finished);
      float animated_y = old_y + (float(y_diff) * animation_finished);
      update_position(animated_x, animated_y);
    }
    editor_area->update();
  });
  move_update_timer.start();
}

static void draw_text_and_bg(
  QPainter& painter,
  const QString& text,
  const HLAttr& attr,
  const HLAttr& def_clrs,
  const QPointF& start,
  const QPointF& end,
  const int offset,
  QFont font
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

void QPaintGrid::draw(QPainter& p, QRect r, const double offset)
{
  const auto& fonts = editor_area->fallback_list();
  QFont cur_font = editor_area->main_font();
  auto font_dims = editor_area->font_dimensions();
  auto font_width = std::get<0>(font_dims);
  auto font_height = std::get<1>(font_dims);
  int start_x = r.left();
  int end_x = r.right();
  int start_y = r.top();
  int end_y = r.bottom();
  QString buffer;
  buffer.reserve(100);
  const HLState* s = editor_area->hl_state();
  const HLAttr& def_clrs = s->default_colors_get();
  u32 cur_font_idx = 0;
  const auto draw_buf = [&](const HLAttr& main, QPointF start, QPointF end) {
    if (buffer.isEmpty()) return;
    draw_text_and_bg(
      p, buffer, main, def_clrs, start, end,
      offset, fonts[cur_font_idx].font()
    );
    buffer.clear();
  };
  const auto get_pos = [&](int x, int y, int num_chars) {
    QPointF tl(x * font_width, y * font_height);
    QPointF br((x + num_chars) * font_width, (y + 1) * font_height);
    return std::tuple {tl, br};
  };
  for(int y = start_y; y <= end_y && y < rows; ++y)
  {
    QPointF start = {(double) x * font_width, (double) (y) * font_height};
    std::uint16_t prev_hl_id = UINT16_MAX;
    cur_font_idx = 0;
    for(int cur_x = 0; cur_x < cols; ++cur_x)
    {
      const auto& gc = area[y * cols + cur_x];
      auto font_idx = editor_area->font_for_ucs(gc.ucs);
      if (font_idx != cur_font_idx
          && !(gc.text.isEmpty() || gc.text.at(0).isSpace()))
      {
        auto [top_left, bot_right] = get_pos(cur_x, y, 1);
        QPointF buf_end = {top_left.x(), top_left.y() + font_height};
        draw_buf(s->attr_for_id(prev_hl_id), start, buf_end);
        start = top_left;
        cur_font_idx = font_idx;
      }
      if (gc.double_width)
      {
        auto [top_left, bot_right] = get_pos(cur_x, y, 2);
        QPointF buf_end = {top_left.x(), top_left.y() + font_height};
        draw_buf(s->attr_for_id(prev_hl_id), start, buf_end);
        buffer.append(gc.text);
        draw_buf(s->attr_for_id(gc.hl_id), top_left, bot_right);
        start = {bot_right.x(), bot_right.y() - font_height};
        prev_hl_id = gc.hl_id;
      }
      else if (gc.hl_id == prev_hl_id)
      {
        buffer.append(gc.text);
        continue;
      }
      else
      {
        auto [top_left, bot_right] = get_pos(cur_x, y, 1);
        draw_buf(s->attr_for_id(prev_hl_id), start, bot_right);
        start = top_left;
        buffer.append(gc.text);
        prev_hl_id = gc.hl_id;
      }
    }
    QPointF bot_right(cols * font_width, (y + 1) * font_height);
    draw_buf(s->attr_for_id(prev_hl_id), start, bot_right);
  }
}

void QPaintGrid::process_events()
{
  QPainter p(&pixmap);
  const QColor bg = editor_area->default_bg();
  const auto offset = editor_area->font_offset();
  while(!evt_q.empty())
  {
    const auto& evt = evt_q.front();
    switch(evt.type)
    {
      case PaintKind::Clear:
        p.fillRect(pixmap.rect(), bg);
        break;
      case PaintKind::Redraw:
        draw(p, {0, 0, cols, rows}, offset);
        clear_event_queue();
        return;
      case PaintKind::Draw:
        draw(p, evt.rect, offset);
        break;
    }
    evt_q.pop();
  }
}

void QPaintGrid::update_position(double new_x, double new_y)
{
  auto&& [font_width, font_height] = editor_area->font_dimensions();
  top_left = {new_x * font_width, new_y * font_height};
}
