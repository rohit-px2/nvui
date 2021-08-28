#include "editor.hpp"
#include "grid.hpp"
#include "utils.hpp"

scalers::time_scaler GridBase::scroll_scaler = scalers::oneminusexpo2negative10;
scalers::time_scaler GridBase::move_scaler = scalers::oneminusexpo2negative10;

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
  snapshots.clear(); // Outdated
}

void QPaintGrid::set_pos(u16 new_x, u16 new_y)
{
  if (!editor_area->animations_enabled())
  {
    GridBase::set_pos(new_x, new_y);
    update_position(new_x, new_y);
    return;
  }
  old_move_x = cur_left;
  old_move_y = cur_top;
  dest_move_x = new_x;
  dest_move_y = new_y;
  auto interval = editor_area->move_animation_frametime();
  move_animation_time = editor_area->move_animation_duration();
  if (move_update_timer.interval() != interval)
  {
    move_update_timer.setInterval(interval);
  }
  GridBase::set_pos(new_x, new_y);
  if (!move_update_timer.isActive()) move_update_timer.start();
}

void QPaintGrid::draw_text_and_bg(
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
  Q_UNUSED(offset);
  const QStaticText* static_text = nullptr;
  using key_type = decltype(text_cache)::key_type;
  key_type key = {text, attr.font_opts};
  font.setItalic(attr.font_opts & FontOpts::Italic);
  font.setBold(attr.font_opts & FontOpts::Bold);
  font.setUnderline(attr.font_opts & FontOpts::Underline);
  font.setStrikeOut(attr.font_opts & FontOpts::Strikethrough);
  painter.setFont(font);
  static_text = text_cache.get(key);
  if (!static_text)
  {
    QStaticText temp {text};
    temp.setTextFormat(Qt::PlainText);
    temp.setPerformanceHint(QStaticText::AggressiveCaching);
    temp.prepare(QTransform(), font);
    text_cache.put(key, std::move(temp));
    static_text = text_cache.get(key);
  }
  Color fg = attr.fg().value_or(*def_clrs.fg());
  Color bg = attr.bg().value_or(*def_clrs.bg());
  if (attr.reverse) std::swap(fg, bg);
  const QRectF rect = {start, end};
  painter.setClipRect(rect);
  painter.fillRect(rect, QColor(bg.r, bg.g, bg.b));
  painter.setPen(QColor(fg.r, fg.g, fg.b));
  painter.drawStaticText(start, *static_text);
}

void QPaintGrid::draw(QPainter& p, QRect r, const double offset)
{
  const auto& fonts = editor_area->fallback_list();
  QFont cur_font = editor_area->main_font();
  auto font_dims = editor_area->font_dimensions();
  auto font_width = std::get<0>(font_dims);
  auto font_height = std::get<1>(font_dims);
  //int start_x = r.left();
  //int end_x = r.right();
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


void QPaintGrid::render(QPainter& p)
{
  auto&& [font_width, font_height] = editor_area->font_dimensions();
  QRectF rect(top_left.x(), top_left.y(), pixmap.width(), pixmap.height());
  auto snapshot_height = pixmap.height();
  if (!editor_area->animations_enabled() || !is_scrolling)
  {
    p.drawPixmap(pos(), pixmap);
    return;
  }
  p.fillRect(rect, editor_area->default_bg());
  float cur_scroll_y = current_scroll_y * font_height;
  float cur_snapshot_top = viewport.topline * font_height;
  u32 min_topline = viewport.topline;
  u32 max_botline = viewport.botline;
  for(auto it = snapshots.rbegin(); it != snapshots.rend(); ++it)
  {
    const auto& snapshot = *it;
    QRectF r;
    float snapshot_top = snapshot.vp.topline * font_height;
    float offset = snapshot_top - cur_scroll_y;
    auto pixmap_top = top_left.y() + offset;
    QPointF pt;
    if (snapshot.vp.topline < min_topline)
    {
      auto height = (min_topline - snapshot.vp.topline) * font_height;
      height = std::min(height, float(snapshot_height));
      min_topline = snapshot.vp.topline;
      r = QRect(0, 0, pixmap.width(), height);
      pt = {top_left.x(), pixmap_top};
    }
    else if (snapshot.vp.botline > max_botline)
    {
      auto height = (snapshot.vp.botline - max_botline) * font_height;
      height = std::min(height, float(snapshot_height));
      max_botline = snapshot.vp.botline;
      r = QRect(0, snapshot_height - height, pixmap.width(), height);
      pt = {top_left.x(), pixmap_top + pixmap.height() - height};
    }
    QRectF draw_rect = {top_left, r.size()};
    if (!r.isNull() && rect.contains(draw_rect))
    {
      p.drawPixmap(pt, snapshot.image, r);
    }
  }
  float offset = cur_snapshot_top - cur_scroll_y;
  QPointF pt = {top_left.x(), top_left.y() + offset};
  p.drawPixmap(pt, pixmap);
}

void QPaintGrid::viewport_changed(Viewport vp)
{
  if (!editor_area->animations_enabled() || viewport.topline == vp.topline)
  {
    GridBase::viewport_changed(vp);
    return;
  }
  /// Logic taken from Keith Simmons' explanation of Neovide's smooth
  /// scrolling, see
  /// here: http://02credits.com/blog/day96-neovide-smooth-scrolling/
  auto dest_topline = vp.topline;
  start_scroll_y = current_scroll_y;
  destination_scroll_y = static_cast<float>(dest_topline);
  snapshots.push_back({viewport, pixmap});
  if (snapshots.size() > editor_area->snapshot_limit())
  {
    snapshots.erase(snapshots.begin());
  }
  GridBase::viewport_changed(vp);
  auto interval = editor_area->scroll_animation_frametime();
  scroll_animation_time = editor_area->scroll_animation_duration();
  if (scroll_animation_timer.interval() != interval)
  {
    scroll_animation_timer.setInterval(interval);
  }
  is_scrolling = true;
  if (!scroll_animation_timer.isActive()) scroll_animation_timer.start();
}

void QPaintGrid::update_position(double new_x, double new_y)
{
  auto&& [font_width, font_height] = editor_area->font_dimensions();
  top_left = {new_x * font_width, new_y * font_height};
}

void QPaintGrid::initialize_cache()
{
  QObject::connect(editor_area, &EditorArea::font_changed, this, [&] {
    text_cache.clear();
  });
}

void QPaintGrid::initialize_scroll_animation()
{
  scroll_animation_timer.callOnTimeout([this] {
    auto timer_interval = scroll_animation_timer.interval();
    scroll_animation_time -= float(timer_interval) / 1000.f;
    if (scroll_animation_time <= 0.f)
    {
      scroll_animation_timer.stop();
      is_scrolling = false;
      snapshots.clear();
    }
    else
    {
      auto diff = destination_scroll_y - start_scroll_y;
      auto duration = editor_area->scroll_animation_duration();
      auto animation_left = scroll_animation_time / duration;
      float animation_finished = 1.0f - animation_left;
      float scale = scroll_scaler(animation_finished);
      current_scroll_y = start_scroll_y + (diff * scale);
    }
    editor_area->update();
  });
}

void QPaintGrid::initialize_move_animation()
{
  move_update_timer.callOnTimeout([this] {
    auto ms_interval = move_update_timer.interval();
    move_animation_time -= float(ms_interval) / 1000.f;
    if (move_animation_time <= 0)
    {
      move_update_timer.stop();
      update_position(dest_move_x, dest_move_y);
    }
    else
    {
      auto x_diff = dest_move_x - old_move_x;
      auto y_diff = dest_move_y - old_move_y;
      auto duration = editor_area->move_animation_duration();
      auto animation_left = move_animation_time / duration;
      float animation_finished = 1.0f - animation_left;
      float scale = move_scaler(animation_finished);
      cur_left = old_move_x + (float(x_diff) * scale);
      cur_top = old_move_y + (float(y_diff) * scale);
      update_position(cur_left, cur_top);
    }
    editor_area->update();
  });
}
