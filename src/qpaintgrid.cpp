#include "qpaintgrid.hpp"
#include "utils.hpp"
#include <QPainterPath>
#include "qeditor.hpp"

struct FontDecorationPaintPath
{
  QPainterPath path;
  double line_thickness;
};

static FontDecorationPaintPath calculate_path(
  const QRectF& rect,
  const FontOpts opt,
  const float font_width,
  const float font_height
)
{
  Q_UNUSED(font_height);
  FontDecorationPaintPath paint_path;
  auto& p = paint_path.path;
  auto& thickness = paint_path.line_thickness;
  /// Lines always go for the length of the rectangle.
  switch(opt)
  {
    case FontOpts::Underline:
    {
      thickness = 1.f;
      const float offset = std::round(font_height * 0.1f);
      p.moveTo(rect.x(), rect.bottom() - offset - (thickness / 2.f));
      p.lineTo(rect.right(), rect.bottom() - offset - (thickness / 2.f));
      break;
    }
    case FontOpts::Undercurl:
    {
      // Squiggly undercurl
      thickness = 1.f;
      float path_height = std::min(font_height / 5.0f, 3.0f);
      p.moveTo(rect.x(), rect.bottom() - path_height);
      bool top = false;
      /// Ensure each character gets at least one triangle-shape
      /// so that it doesn't look awkward
      const double inc = font_width / 2.f;
      for(double x = rect.x() + inc;; x += inc)
      {
        using namespace std;
        p.lineTo(min(x, rect.right()), rect.bottom() - (path_height * top));
        top = !top;
        if (x >= rect.right()) break;
      }
      break;
    }
    case FontOpts::Strikethrough:
    {
      thickness = 1.f;
      double mid_y = rect.y() + (rect.height() / 2.);
      p.moveTo(rect.x(), mid_y);
      p.lineTo(rect.right(), mid_y);
      break;
    }
    default: break;
  }
  return paint_path;
}

static void set_pen_width(QPainter& painter, double w)
{
  auto pen = painter.pen();
  pen.setWidthF(w);
  painter.setPen(pen);
}

void QPaintGrid::update_pixmap_size()
{
  auto&& [font_width, font_height] = editor_area->font_dimensions();
  pixmap = QPixmap(
    cols * font_width * editor_area->devicePixelRatioF(),
    rows * font_height * editor_area->devicePixelRatioF()
  );
  pixmap.fill(editor_area->default_bg().qcolor());
  pixmap.setDevicePixelRatio(editor_area->devicePixelRatioF());
  send_redraw();
}

void QPaintGrid::set_size(u16 w, u16 h)
{
  GridBase::set_size(w, h);
  update_pixmap_size();
  snapshots.clear(); // Outdated
}

void QPaintGrid::set_pos(double new_x, double new_y)
{
  if (!editor_area->animations_enabled() || is_float())
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

QFont::Weight qfont_weight(const FontOpts& fo)
{
  if (fo & FontOpts::Normal) return QFont::Normal;
  if (fo & FontOpts::Thin) return QFont::Thin;
  if (fo & FontOpts::Light) return QFont::Light;
  if (fo & FontOpts::Medium) return QFont::Medium;
  if (fo & FontOpts::SemiBold) return QFont::DemiBold;
  if (fo & FontOpts::Bold) return QFont::Bold;
  if (fo & FontOpts::ExtraBold) return QFont::ExtraBold;
  return QFont::Normal;
}

QFont::Style qfont_style(const FontOpts& fo)
{
  if (fo & FontOpts::Italic) return QFont::StyleItalic;
  return QFont::StyleNormal;
}

void QPaintGrid::draw_text(
  QPainter& painter,
  const QString& text,
  const Color& fg,
  const std::optional<Color>& sp,
  const QRectF& rect,
  const FontOptions font_opts,
  const QFont& font,
  float font_width,
  float font_height
)
{
  using key_type = decltype(text_cache)::key_type;
  key_type key = {text, font_opts};
  auto w = font::weight_for(font_opts);
  auto s = font::style_for(font_opts);
  if (w == FontOpts::Normal) w = editor_area->default_font_weight();
  if (s == FontOpts::Normal) s = editor_area->default_font_style();
  painter.setFont(font);
  QStaticText* static_text = text_cache.get(key);
  if (!static_text)
  {
    static_text = &text_cache.put(std::move(key), QStaticText {text});
    static_text->setTextFormat(Qt::PlainText);
    static_text->setPerformanceHint(QStaticText::AggressiveCaching);
    static_text->prepare(QTransform(), font);
  }
  auto text_size = static_text->size();
  double y = rect.y();
  y -= (text_size.height() + editor_area->linespacing() - font_height);
  y += (editor_area->linespacing() / 2.);
  painter.setClipRect(rect);
  painter.setPen(fg.qcolor());
  painter.drawStaticText(QPointF {rect.x(), y}, *static_text);
  QRectF line_clip_rect {
    rect.x(), rect.y(),
    static_text->size().width(), font_height
  };
  if (!sp) return;
  painter.setPen(sp.value().qcolor());
  const auto draw_path = [&](const FontOpts fo) {
    auto [path, pen_w] = calculate_path(line_clip_rect, fo, font_width, font_height);
    set_pen_width(painter, pen_w);
    painter.drawPath(path);
  };
  if (font_opts & FontOpts::Underline) draw_path(FontOpts::Underline);
  if (font_opts & FontOpts::Undercurl) draw_path(FontOpts::Undercurl);
  if (font_opts & FontOpts::Strikethrough) draw_path(FontOpts::Strikethrough);
}

void QPaintGrid::draw_text_and_bg(
  QPainter& painter,
  const QString& text,
  const HLAttr& attr,
  const HLAttr& def_clrs,
  const QPointF& start,
  const QPointF& end,
  const int offset,
  const QFont& font,
  float font_width,
  float font_height
)
{
  Q_UNUSED(offset);
  auto [fg, bg, sp] = attr.fg_bg_sp(def_clrs);
  QRectF rect = {start, end};
  painter.setClipRect(rect);
  painter.fillRect(rect, bg.qcolor());
  rect.setWidth(rect.width() + 1.0);
  draw_text(
    painter, text, fg, sp, rect, attr.font_opts, font, font_width, font_height
  );
}

void QPaintGrid::draw(QPainter& p, QRect r, const double offset)
{
  const auto& fonts = editor_area->fallback_list();
  QFont cur_font = editor_area->main_font();
  auto font_dims = editor_area->font_dimensions();
  auto font_width = font_dims.width;
  auto font_height = font_dims.height;
  //int start_x = r.left();
  //int end_x = r.right();
  int start_y = r.top();
  int end_y = r.bottom();
  QString buffer;
  buffer.reserve(100);
  const HLState* s = &editor_area->hlstate();
  const HLAttr& def_clrs = s->default_colors_get();
  u32 cur_font_idx = 0;
  const auto draw_buf = [&](const HLAttr& main, QPointF start, QPointF end) {
    if (buffer.isEmpty()) return;
    reverse_qstring(buffer);
    const auto& attr_font = fonts[cur_font_idx].font_for(main.font_opts);
    draw_text_and_bg(
      p, buffer, main, def_clrs, start, end,
      offset, attr_font, font_width, font_height
    );
    buffer.resize(0);
  };
  const auto get_pos = [&](int x, int y, int num_chars) {
    QPointF tl(x * font_width, y * font_height);
    QPointF br((x + num_chars) * font_width, (y + 1) * font_height);
    return std::pair {tl, br};
  };
  for(int y = start_y; y <= end_y && y < rows; ++y)
  {
    QPointF end = {cols * font_width, (y + 1) * font_height};
    int prev_hl_id = INT_MAX;
    /// Reverse iteration. This prevents text from clipping
    for(int x = cols - 1; x >= 0; --x)
    {
      const auto& gc = area[y * cols + x];
      const auto font_idx = editor_area->font_for_ucs(gc.ucs);
      if (gc.text.isEmpty())
      {
        const auto [tl, br] = get_pos(x + 1, y, 0);
        draw_buf(s->attr_for_id(prev_hl_id), tl, end);
        end = br;
      }
      if (font_idx != cur_font_idx
          && !(gc.text.isEmpty() || gc.text[0].isSpace()))
      {
        const auto [tl, br] = get_pos(x, y, 1);
        QPointF buf_start = {br.x(), br.y() - font_height};
        draw_buf(s->attr_for_id(prev_hl_id), buf_start, end);
        end = br;
        cur_font_idx = font_idx;
      }
      if (gc.double_width)
      {
        // Assume previous buffer already drawn.
        const auto [tl, br] = get_pos(x, y, 2);
        buffer.append(gc.text);
        draw_buf(s->attr_for_id(gc.hl_id), tl, br);
        end = {tl.x(), tl.y() + font_height};
        prev_hl_id = gc.hl_id;
      }
      else if (gc.hl_id == prev_hl_id)
      {
        buffer.append(gc.text);
        continue;
      }
      else
      {
        const auto [tl, br] = get_pos(x, y, 1);
        QPointF start = {br.x(), br.y() - font_height};
        draw_buf(s->attr_for_id(prev_hl_id), start, end);
        end = br;
        buffer.append(gc.text);
        prev_hl_id = gc.hl_id;
      }
    }
    QPointF start = {0, y * font_height};
    draw_buf(s->attr_for_id(prev_hl_id), start, end);
  }
}

void QPaintGrid::process_events()
{
  if (evt_q.empty()) return;
  QPainter p(&pixmap);
  p.setRenderHint(QPainter::TextAntialiasing);
  const QColor bg = editor_area->hlstate().default_bg().qcolor();
  QFontMetrics fm {editor_area->main_font()};
  const auto offset = fm.ascent() + (editor_area->linespacing() / 2.f);
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
        draw(p, evt.draw_info().rect, offset);
        break;
      case PaintKind::Scroll:
      {
        auto dpr = editor_area->devicePixelRatioF();
        auto [font_width, font_height] = editor_area->font_dimensions();
        const auto& [rect, dx, dy] = evt.scroll_info();
        // I love how the rectangle coordinates aren't automatically scaled
        // by the pixmap's device pixel ratio
        QRect r(
          rect.x() * font_width * dpr,
          rect.y() * font_height * dpr,
          rect.width() * font_width * dpr,
          rect.height() * font_height * dpr
        );
        // This would probably be faster using QPixmap::scroll
        // but it doesn't want to work
        p.end();
        QPixmap px = pixmap.copy(r);
        QPoint tl((rect.x() + dx) * font_width, (rect.y() + dy) * font_height);
        p.begin(&pixmap);
        p.drawPixmap(tl, px, px.rect());
        break;
      }
    }
    evt_q.pop();
  }
}

void QPaintGrid::render(QPainter& p)
{
  auto [font_width, font_height] = editor_area->font_dimensions();
  // p (the editor painter) scales ITS OWN coordinates (the QPoint/QPointF)
  // that's the first parameter in every call) by the device pixel ratio
  // However, the coordinates of the grid pixmap are not scaled by the DPR.
  // So we have to take this into account when interacting with the painter.
  auto pixmap_height = pixmap.height() / editor_area->devicePixelRatioF();
  auto pixmap_width = pixmap.width() / editor_area->devicePixelRatioF();
  QRectF rect {top_left, QSize(pixmap_width, pixmap_height)};
  if (!editor_area->animations_enabled() || !is_scrolling)
  {
    p.drawPixmap(pos(), pixmap);
    return;
  }
  QPointF topleft_text = QPointF(top_left.x() / font_width, top_left.y() / font_height);
  p.fillRect(rect, editor_area->hlstate().default_bg().qcolor());
  float cur_scroll_y = current_scroll_y * font_height;
  float cur_snapshot_top = viewport.topline * font_height;
  u32 min_topline = viewport.topline;
  u32 max_botline = viewport.botline;
  for(auto it = snapshots.rbegin(); it != snapshots.rend(); ++it)
  {
    const auto& snapshot = *it;
    float offset = snapshot.vp.topline - current_scroll_y;
    QRectF px_rect;
    QPointF px_pt;
    float top_row = topleft_text.y() + offset;
    if (snapshot.vp.topline < min_topline)
    {
      float height = (min_topline - snapshot.vp.topline);
      height = std::min(height, float(rows));
      min_topline = snapshot.vp.topline;
      px_rect = QRectF(0, 0, cols, height);
      px_pt = QPointF(topleft_text.x(), top_row);
    }
    else if (snapshot.vp.botline > max_botline)
    {
      float height = (snapshot.vp.botline - max_botline);
      height = std::min(height, float(rows));
      max_botline = snapshot.vp.botline;
      px_rect = QRectF(0, (rows - height), cols, height);
      px_pt = QPointF(topleft_text.x(), top_row + (rows - height));
    }
    else continue;
    double dpr = editor_area->devicePixelRatioF();
    p.drawPixmap(
      QPointF {px_pt.x() * font_width, px_pt.y() * font_height},
      snapshot.image,
      QRectF {
        px_rect.x() * font_width * dpr,
        px_rect.y() * font_height * dpr,
        px_rect.width() * font_width * dpr,
        px_rect.height() * font_height * dpr
      }
    );
  }
  float offset = cur_snapshot_top - cur_scroll_y;
  QPointF pt = {top_left.x(), top_left.y() + offset};
  p.drawPixmap(pt, pixmap);
}

void QPaintGrid::viewport_changed(Viewport vp)
{
  if (!modified) vp = viewport;
  if (!editor_area->animations_enabled() || viewport.topline == vp.topline)
  {
    GridBase::viewport_changed(vp);
    modified = false;
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
  modified = false;
}

void QPaintGrid::update_position(double new_x, double new_y)
{
  auto&& [font_width, font_height] = editor_area->font_dimensions();
  top_left = {new_x * font_width, new_y * font_height};
}

void QPaintGrid::init_connections()
{
  QObject::connect(editor_area, &QEditor::font_changed, this, [&] {
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

void QPaintGrid::draw_cursor(QPainter& painter, const Cursor& cursor)
{
  const auto [font_width, font_height] = editor_area->font_dimensions();
  const HLState* hl = &editor_area->hlstate();
  auto pos_opt = cursor.pos();
  if (!pos_opt) return;
  auto pos = pos_opt.value();
  std::size_t idx = pos.row * cols + pos.col;
  if (idx >= area.size()) return;
  const auto& gc = area[idx];
  float scale_factor = 1.0f;
  if (gc.double_width) scale_factor = 2.0f;
  auto rect_opt = cursor.rect(font_width, font_height, scale_factor, true);
  if (!rect_opt) return;
  auto [rect, hl_id, should_draw_text, opacity] = rect_opt.value();
  const HLAttr& cursor_attr = hl->attr_for_id(hl_id);
  Color fg = cursor_attr.fg().value_or(hl->default_fg());
  Color bg = cursor_attr.bg().value_or(hl->default_bg());
  if (hl_id == 0 || cursor_attr.reverse) std::swap(fg, bg);
  painter.setOpacity(opacity);
  painter.fillRect(rect, bg.qcolor());
  painter.setOpacity(1.0);
  if (should_draw_text)
  {
    float left = (x + pos.col) * font_width;
    float top = (y + pos.row) * font_height;
    const QPointF bot_left {left, top};
    auto font_idx = editor_area->font_for_ucs(gc.ucs);
    FontOptions opts = cursor_attr.font_opts == FontOpts::Normal
      ? hl->attr_for_id(gc.hl_id).font_opts
      : cursor_attr.font_opts;
    QFont chosen_font = editor_area->fallback_list()[font_idx].font();
    QRectF text_rect(left, top, font_width * scale_factor * 5., font_height);
    draw_text(
      painter, gc.text, fg, cursor_attr.sp(), text_rect,
      opts, chosen_font, font_width, font_height
    );
  }
}

