#include "wineditor.hpp"
#include "d2deditor.hpp"
#include "direct2dpaintgrid.hpp"
#include "utils.hpp"
#include <d2d1.h>

using Microsoft::WRL::ComPtr;

//#include <comdef.h>
//static void print_error(HRESULT hr)
//{
  //fmt::print("HRESULT error: {}\n", _com_error(hr).ErrorMessage());
//}

void D2DPaintGrid::set_size(u16 w, u16 h)
{
  GridBase::set_size(w, h);
  update_bitmap_size();
  for(auto& snapshot : snapshots)
  {
    SafeRelease(&snapshot.image);
  }
  snapshots.clear();
}

void D2DPaintGrid::update_bitmap_size()
{
  auto&& [font_width, font_height] = editor_area->font_dimensions();
  u32 width = std::ceil(cols * font_width);
  u32 height = std::ceil(rows * font_height);
  editor_area->resize_bitmap(context, &bitmap, width, height);
  context->SetTarget(bitmap);
  context->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
}

void D2DPaintGrid::initialize_context()
{
  editor_area->create_context(&context, &bitmap, 0, 0);
  context->SetTarget(bitmap);
  context->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
}

void D2DPaintGrid::initialize_cache()
{
  QObject::connect(editor_area, &EditorArea::font_changed, this, [&] {
    layout_cache.clear();
  });
}

void D2DPaintGrid::initialize_move_animation()
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
      // What % of the animation is left (between 0 and 1)
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

void D2DPaintGrid::initialize_scroll_animation()
{
  scroll_animation_timer.callOnTimeout([this] {
    auto timer_interval = scroll_animation_timer.interval();
    scroll_animation_time -= float(timer_interval) / 1000.f;
    if (scroll_animation_time <= 0.f)
    {
      scroll_animation_timer.stop();
      is_scrolling = false;
      for(auto& snapshot : snapshots) SafeRelease(&snapshot.image);
      snapshots.clear();
    }
    else
    {
      auto diff = dest_scroll_y - start_scroll_y;
      auto duration = editor_area->scroll_animation_duration();
      auto animation_left = scroll_animation_time / duration;
      float animation_finished = 1.0f - animation_left;
      float scaled = scroll_scaler(animation_finished);
      current_scroll_y = start_scroll_y + (diff * scaled);
    }
    editor_area->update();
  });
}

void D2DPaintGrid::process_events()
{
  context->BeginDraw();
  ID2D1SolidColorBrush* fg_brush = nullptr;
  ID2D1SolidColorBrush* bg_brush = nullptr;
  u32 fg = editor_area->default_fg().rgb();
  u32 bg = editor_area->default_bg().rgb();
  context->CreateSolidColorBrush(d2color(fg), &fg_brush);
  context->CreateSolidColorBrush(d2color(bg), &bg_brush);
  while(!evt_q.empty())
  {
    const auto& evt = evt_q.front();
    switch(evt.type)
    {
      case PaintKind::Clear:
      {
        d2rect r = source_rect();
        bg_brush->SetColor(d2color(bg));
        context->FillRectangle(r, bg_brush);
        break;
      }
      case PaintKind::Redraw:
        draw(context, {0, 0, cols, rows}, fg_brush, bg_brush);
        clear_event_queue();
        break;
      case PaintKind::Draw:
        draw(context, evt.rect, fg_brush, bg_brush);
        break;
    }
    if (!evt_q.empty()) evt_q.pop();
  }
  SafeRelease(&fg_brush);
  SafeRelease(&bg_brush);
  context->EndDraw();
}

static void draw_text_decorations(
  ID2D1RenderTarget* context,
  const FontOpts fo,
  D2D1_POINT_2F seq_start,
  D2D1_POINT_2F seq_end,
  float font_width,
  float font_height,
  ID2D1SolidColorBrush& brush
)
{
  using d2pt = D2DPaintGrid::d2pt;
  switch(fo)
  {
    case FontOpts::Underline:
    {
      const float line_thickness = 1.0f;
      const float line_mid = seq_end.y
        - std::round(font_height * 0.1f)
        - (line_thickness / 2.f);
      context->DrawLine(
        {seq_start.x, line_mid}, {seq_end.x, line_mid}, &brush, line_thickness
      );
      break;
    }
    case FontOpts::Strikethrough:
    {
      float line_thickness = 1.0f;
      float line_mid = seq_start.y + (font_height - line_thickness) / 2.f;
      context->DrawLine(
        {seq_start.x, line_mid}, {seq_end.x, line_mid}, &brush, line_thickness
      );
      break;
    }
    case FontOpts::Undercurl:
    {
      float line_thickness = 1.0f;
      float line_height = std::min(font_height / 5.0f, 3.0f);
      const double inc = font_width / 2.f;
      d2pt prev_pos = {seq_start.x, seq_end.y - line_height};
      bool top = false;
      for(float x = seq_start.x + inc;; x += inc)
      {
        d2pt cur_pos = {std::min(x, seq_end.x), seq_end.y - (line_height * top)};
        context->DrawLine(prev_pos, cur_pos, &brush, line_thickness);
        prev_pos = cur_pos;
        top = !top;
        if (x >= seq_end.x) break;
      }
      break;
    }
    default: break;
  }
}

void D2DPaintGrid::draw_text(
  ID2D1RenderTarget& target,
  const QString& text,
  const Color& fg,
  const Color& sp,
  const FontOptions font_opts,
  D2D1_POINT_2F top_left,
  D2D1_POINT_2F bot_right,
  float font_width,
  float font_height,
  ID2D1SolidColorBrush& fg_brush,
  IDWriteTextFormat* text_format,
  bool clip
)
{
  if (clip)
  {
    target.PushAxisAlignedClip({
      top_left.x, top_left.y, bot_right.x, bot_right.y
    }, D2D1_ANTIALIAS_MODE_ALIASED);
  }
  fg_brush.SetColor(d2color(fg.to_uint32()));
  using key_type = decltype(layout_cache)::key_type;
  IDWriteTextLayout* old_text_layout = nullptr;
  IDWriteTextLayout1* text_layout = nullptr;
  key_type key = {text, font_opts};
  auto objptr = layout_cache.get(key);
  if (objptr) text_layout = *objptr;
  else
  {
    HRESULT hr;
    auto* factory = editor_area->dwrite_factory();
    hr = factory->CreateTextLayout(
      (LPCWSTR) text.constData(),
      text.size(),
      text_format,
      // Sometimes the text clips weirdly & adding
      // to the width solves it. It's probably because
      // the text is just a little wider than the max width we set
      // so we increase the max width by a little here.
      bot_right.x - top_left.x + 1000.f,
      bot_right.y - top_left.y,
      &old_text_layout
    );
    if (FAILED(hr)) return;
    // IDWriteTextLayout1 can set char spacing
    hr = old_text_layout->QueryInterface(&text_layout);
    if (FAILED(hr)) { SafeRelease(&old_text_layout); return; }
    DWRITE_TEXT_RANGE text_range {0, (UINT32) text.size()};
    auto charspace = editor_area->charspacing();
    if (charspace)
    {
      text_layout->SetCharacterSpacing(0, float(charspace), 0, text_range);
    }
    if (font_opts & FontOpts::Italic)
    {
      text_layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, text_range);
    }
    if (font_opts & FontOpts::Bold)
    {
      text_layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, text_range);
    }
    layout_cache.put(key, text_layout);
  }
  auto offset = float(editor_area->linespacing()) / 2.f;
  D2D1_POINT_2F text_pt = {top_left.x, top_left.y + offset};
  target.DrawTextLayout(
    text_pt,
    text_layout,
    &fg_brush,
    D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT
  );
  fg_brush.SetColor(D2D1::ColorF(sp.to_uint32()));
  SafeRelease(&old_text_layout);
  const auto draw_path = [&](const FontOpts fo) {
    draw_text_decorations(
      &target, fo, top_left, bot_right, font_width, font_height, fg_brush
    );
  };
  if (font_opts & FontOpts::Underline) draw_path(FontOpts::Underline);
  if (font_opts & FontOpts::Undercurl) draw_path(FontOpts::Undercurl);
  if (font_opts & FontOpts::Strikethrough) draw_path(FontOpts::Strikethrough);
  if (clip) target.PopAxisAlignedClip();
}

void D2DPaintGrid::draw_bg(
  ID2D1RenderTarget& target,
  const Color& bg,
  D2D1_POINT_2F top_left,
  D2D1_POINT_2F bot_right,
  ID2D1SolidColorBrush& brush
)
{
  brush.SetColor(D2D1::ColorF(bg.to_uint32()));
  target.FillRectangle({top_left.x, top_left.y, bot_right.x, bot_right.y}, &brush);
}

void D2DPaintGrid::draw_bg(
  ID2D1RenderTarget& target,
  const Color& bg,
  D2D1_RECT_F rect,
  ID2D1SolidColorBrush& brush
)
{
  draw_bg(target, bg, {rect.left, rect.top}, {rect.right, rect.bottom}, brush);
}

void D2DPaintGrid::draw_text_and_bg(
  ID2D1RenderTarget* context,
  const QString& buf,
  const HLAttr& attr,
  const HLAttr& fallback,
  D2D1_POINT_2F start,
  D2D1_POINT_2F end,
  float font_width,
  float font_height,
  IDWriteTextFormat* text_format,
  ID2D1SolidColorBrush* fg_brush,
  ID2D1SolidColorBrush* bg_brush
)
{
  auto [fg, bg, sp] = attr.fg_bg_sp(fallback);
  auto bg_tl = D2D1::Point2F(std::floor(start.x), start.y);
  draw_bg(*context, bg, bg_tl, end, *bg_brush);
  draw_text(
    *context, buf, fg, sp, attr.font_opts, start, end,
    font_width, font_height, *fg_brush, text_format
  );
}

void D2DPaintGrid::draw(
  ID2D1RenderTarget* context,
  QRect r,
  ID2D1SolidColorBrush* fg_brush,
  ID2D1SolidColorBrush* bg_brush
)
{
  auto target_size = context->GetSize();
  const auto& fonts = editor_area->fallback_list();
  //const int start_x = r.left(), end_x = r.right();
  const int start_y = r.top(), end_y = r.bottom();
  const auto font_dims = editor_area->font_dimensions();
  const float font_width = font_dims.width;
  const float font_height = font_dims.height;
  const HLState* s = editor_area->hl_state();
  QString buffer;
  buffer.reserve(100);
  const auto& def_clrs = s->default_colors_get();
  u32 cur_font_idx = 0;
  const auto get_pos = [&](int x, int y, int num_chars) {
    d2pt tl = {x * font_width, y * font_height};
    d2pt br = {(x + num_chars) * font_width, (y + 1) * font_height};
    return std::pair {tl, br};
  };
  const auto draw_buf = [&](const HLAttr& main, d2pt start, d2pt end) {
    if (buffer.isEmpty()) return;
    const auto& tf = fonts[cur_font_idx];
    reverse_qstring(buffer);
    draw_text_and_bg(
      context, buffer, main, def_clrs, start, end, font_width, font_height,
      tf, fg_brush, bg_brush
    );
    buffer.resize(0);
  };
  for(int y = start_y; y <= end_y && y < rows; ++y)
  {
    d2pt end = {target_size.width, (y + 1) * font_height};
    int prev_hl_id = 0;
    if (y * cols + (cols - 1) < (int) area.size())
    {
      prev_hl_id = area[y * cols + cols - 1].hl_id;
    }
    /// Reverse iteration. This prevents text from clipping
    for(int x = cols - 1; x >= 0; --x)
    {
      const auto& gc = area[y * cols + x];
      const auto font_idx = editor_area->font_for_ucs(gc.ucs);
      /// Neovim double-width characters have an empty string after them.
      /// Iterating from right to left we see the empty string first,
      /// then the double width character, which is why we have to draw
      /// the buffer as soon as we see the empty string.
      if (gc.text.isEmpty())
      {
        const auto [tl, br] = get_pos(x + 1, y, 0);
        draw_buf(s->attr_for_id(prev_hl_id), tl, end);
        end = br;
      }
      if (font_idx != cur_font_idx && !gc.text[0].isSpace())
      {
        const auto [tl, br] = get_pos(x + 1, y, 0);
        draw_buf(s->attr_for_id(prev_hl_id), tl, end);
        end = br;
        cur_font_idx = font_idx;
      }
      if (gc.double_width)
      {
        // Assume previous text has already been drawn.
        const auto [tl, br] = get_pos(x, y, 2);
        buffer.append(gc.text);
        draw_buf(s->attr_for_id(gc.hl_id), tl, br);
        end = get_pos(x, y, 0).second;
        prev_hl_id = gc.hl_id;
      }
      else if (gc.hl_id == prev_hl_id)
      {
        buffer.append(gc.text);
        continue;
      }
      else
      {
        const auto [tl, br] = get_pos(x + 1, y, 0);
        draw_buf(s->attr_for_id(prev_hl_id), tl, end);
        end = br;
        buffer.append(gc.text);
        prev_hl_id = gc.hl_id;
      }
    }
    d2pt start = {0, y * font_height};
    draw_buf(s->attr_for_id(prev_hl_id), start, end);
  }
}

D2DPaintGrid::d2pt D2DPaintGrid::pos() const
{
  auto&& [font_width, font_height] = editor_area->font_dimensions();
  return {x * font_width, y * font_height};
}

D2DPaintGrid::d2rect D2DPaintGrid::rect() const
{
  auto size = context->GetSize();
  int left = top_left.x();
  int top = top_left.y();
  float right = left + size.width;
  float bottom = top + size.height;
  return D2D1::RectF(left, top, right, bottom);
}

D2DPaintGrid::d2rect D2DPaintGrid::source_rect() const
{
  auto size = context->GetSize();
  return D2D1::RectF(0, 0, size.width, size.height);
}

void D2DPaintGrid::set_pos(u16 new_x, u16 new_y)
{
  if (!editor_area->animations_enabled())
  {
    move_update_timer.stop();
    GridBase::set_pos(new_x, new_y);
    update_position(new_x, new_y);
    return;
  }
  old_move_x = cur_left;
  old_move_y = cur_top;
  dest_move_x = new_x;
  dest_move_y = new_y;
  move_animation_time = editor_area->move_animation_duration();
  auto interval = editor_area->move_animation_frametime();
  if (move_update_timer.interval() != interval)
  {
    move_update_timer.setInterval(interval);
  }
  GridBase::set_pos(new_x, new_y);
  if (!move_update_timer.isActive()) move_update_timer.start();
}

void D2DPaintGrid::update_position(double x, double y)
{
  auto&& [font_width, font_height] = editor_area->font_dimensions();
  top_left = {x * font_width, y * font_height};
}

void D2DPaintGrid::viewport_changed(Viewport vp)
{
  if (!modified) vp = viewport;
  // Thanks to Keith Simmons, the Neovide developer for explaining
  // his implementation of Neovide's smooth scrolling in his blog post
  // at http://02credits.com/blog/day96-neovide-smooth-scrolling.
  if (!editor_area->animations_enabled() || viewport.topline == vp.topline)
  {
    GridBase::viewport_changed(vp);
    modified = false;
    return;
  }
  auto dest_topline = vp.topline;
  start_scroll_y = current_scroll_y;
  dest_scroll_y = dest_topline;
  snapshots.push_back({viewport, copy_bitmap(bitmap)});
  if (snapshots.size() > editor_area->snapshot_limit())
  {
    SafeRelease(&snapshots[0].image);
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

ID2D1Bitmap1* D2DPaintGrid::copy_bitmap(ID2D1Bitmap1* src)
{
  assert(src);
  ID2D1Bitmap1* dst = nullptr;
  auto sz = src->GetPixelSize();
  editor_area->resize_bitmap(context, &dst, sz.width, sz.height);
  auto tl = D2D1::Point2U(0, 0);
  auto src_rect = D2D1::RectU(0, 0, sz.width, sz.height);
  dst->CopyFromBitmap(&tl, src, &src_rect);
  return dst;
}

void D2DPaintGrid::render(ID2D1RenderTarget* render_target)
{
  auto&& [font_width, font_height] = editor_area->font_dimensions();
  auto sz = bitmap->GetPixelSize();
  d2rect r = rect();
  auto bg = editor_area->default_bg().rgb();
  QRectF rect {top_left.x(), top_left.y(), (qreal) sz.width, (qreal) sz.height};
  ID2D1SolidColorBrush* bg_brush = nullptr;
  render_target->CreateSolidColorBrush(D2D1::ColorF(bg), &bg_brush);
	bg_brush->SetOpacity(1.0f);
  // Sometimes in multigrid mode the root grid can 'peek through'
  // by 1 pixel, because of rounding error
  // Increase the width of the fill rectangle by 1 on each side
  // to correct this
  auto fill_rect = D2D1::RectF(r.left - 1, r.top, r.right + 1, r.bottom);
  render_target->FillRectangle(fill_rect, bg_brush);
  SafeRelease(&bg_brush);
  if (!editor_area->animations_enabled() || !is_scrolling)
  {
    render_target->DrawBitmap(
      bitmap,
      &r,
      1.0f,
      D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
    );
    return;
  }
  render_target->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_ALIASED);
  float cur_scroll_y = current_scroll_y * font_height;
  float cur_snapshot_top = viewport.topline * font_height;
  for(auto it = snapshots.rbegin(); it != snapshots.rend(); ++it)
  {
    const auto& snapshot = *it;
    float snapshot_top = snapshot.vp.topline * font_height;
    float offset = snapshot_top - cur_scroll_y;
    auto pixmap_top = top_left.y() + offset;
    d2pt pt = D2D1::Point2F(top_left.x(), pixmap_top);
    r = D2D1::RectF(pt.x, pt.y, pt.x + sz.width, pt.y + sz.height);
    render_target->DrawBitmap(
      snapshot.image,
      &r,
      1.0f,
      D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
    );
  }
  float offset = cur_snapshot_top - cur_scroll_y;
  d2pt pt = D2D1::Point2F(top_left.x(), top_left.y() + offset);
  r = D2D1::RectF(pt.x, pt.y, pt.x + sz.width, pt.y + sz.height);
  render_target->DrawBitmap(
    bitmap,
    &r,
    1.0f,
    D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
  );
  render_target->PopAxisAlignedClip();
}

void D2DPaintGrid::draw_cursor(ID2D1RenderTarget *target, const Cursor &cursor)
{
  const auto& text_formats = editor_area->fallback_list();
  const HLState* hl = editor_area->hl_state();
  const auto [font_width, font_height] = editor_area->font_dimensions();
  const auto pos_opt = cursor.pos();
  if (!pos_opt) return;
  const auto pos = pos_opt.value();
  std::size_t idx = pos.row * cols + pos.col;
  if (idx >= area.size()) return;
  const auto& gc = area[idx];
  float scale_factor = 1.0f;
  if (gc.double_width) scale_factor = 2.0f;
  const CursorRect rect = *cursor.rect(font_width, font_height, scale_factor);
  ID2D1SolidColorBrush* brush = nullptr;
  const HLAttr& attr = hl->attr_for_id(rect.hl_id);
  auto [fg, bg, sp] = attr.fg_bg_sp(hl->default_colors_get());
  if (attr.hl_id == 0) std::swap(fg, bg);
  target->CreateSolidColorBrush(D2D1::ColorF(bg.to_uint32()), &brush);
  const QRectF& r = rect.rect;
  auto fill_rect = D2D1::RectF(r.left(), r.top(), r.right(), r.bottom());
  // Draw cursor background
  brush->SetOpacity(rect.opacity);
  draw_bg(*target, bg, fill_rect, *brush);
  brush->SetOpacity(1.0f);
  if (rect.should_draw_text)
  {
    fill_rect.right = std::max(fill_rect.right, fill_rect.left + font_width);
    // If the rect exists, the pos must exist as well.
    auto font_idx = editor_area->font_for_ucs(gc.ucs);
    assert(font_idx < text_formats.size());
    FontOptions fo = attr.font_opts == FontOpts::Normal
      ? hl->attr_for_id(gc.hl_id).font_opts
      : attr.font_opts;
    const auto start = D2D1::Point2F((x + pos.col) * font_width, (y + pos.row) * font_height);
    const auto end = D2D1::Point2F(start.x + (scale_factor * font_width), start.y + font_height);
    draw_text(
      *target, gc.text, fg, sp, fo, start, end,
      font_width, font_height, *brush, text_formats[font_idx], true
    );
  }
  SafeRelease(&brush);
}

D2DPaintGrid::~D2DPaintGrid()
{
  for(auto& snapshot : snapshots) SafeRelease(&snapshot.image);
  SafeRelease(&bitmap);
  SafeRelease(&context);
}

void D2DPaintGrid2::set_size(u16 w, u16 h)
{
  GridBase::set_size(w, h);
  update_render_target();
  snapshots.clear();
}

void D2DPaintGrid2::update_render_target()
{
  auto&& [font_width, font_height] = editor_area->font_dimensions();
  u32 width = std::ceil(cols * font_width);
  u32 height = std::ceil(rows * font_height);
  auto [target, surface] = editor_area->create_render_target(width, height);
  render_target = std::move(target);
  bitmap = std::move(surface);
  render_target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
  render_target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
}

void D2DPaintGrid2::init_connections()
{
  QObject::connect(editor_area, &D2DEditor::font_changed, this, [this] {
    layout_cache.clear();
  });
  QObject::connect(editor_area, &D2DEditor::render_targets_updated, this, [this] {
    update_render_target();
    send_redraw();
  });
}

void D2DPaintGrid2::initialize_move_animation()
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
      // What % of the animation is left (between 0 and 1)
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

void D2DPaintGrid2::initialize_scroll_animation()
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
      auto diff = dest_scroll_y - start_scroll_y;
      auto duration = editor_area->scroll_animation_duration();
      auto animation_left = scroll_animation_time / duration;
      float animation_finished = 1.0f - animation_left;
      float scaled = scroll_scaler(animation_finished);
      current_scroll_y = start_scroll_y + (diff * scaled);
    }
    editor_area->update();
  });
}

void D2DPaintGrid2::process_events()
{
  if (evt_q.empty()) return;
  auto* context = render_target.Get();
  context->BeginDraw();
  ComPtr<ID2D1SolidColorBrush> fg_brush = nullptr;
  ComPtr<ID2D1SolidColorBrush> bg_brush = nullptr;
  u32 fg = editor_area->default_fg().to_uint32();
  u32 bg = editor_area->default_bg().to_uint32();
  context->CreateSolidColorBrush(d2color(fg), &fg_brush);
  context->CreateSolidColorBrush(d2color(bg), &bg_brush);
  while(!evt_q.empty())
  {
    const auto& evt = evt_q.front();
    switch(evt.type)
    {
      case PaintKind::Clear:
      {
        d2rect r = source_rect();
        bg_brush->SetColor(d2color(bg));
        context->FillRectangle(r, bg_brush.Get());
        break;
      }
      case PaintKind::Redraw:
        draw(context, {0, 0, cols, rows}, fg_brush.Get(), bg_brush.Get());
        clear_event_queue();
        break;
      case PaintKind::Draw:
        draw(context, evt.rect, fg_brush.Get(), bg_brush.Get());
        break;
    }
    if (!evt_q.empty()) evt_q.pop();
  }
  context->EndDraw();
}

void D2DPaintGrid2::draw_text(
  ID2D1RenderTarget& target,
  const QString& text,
  const Color& fg,
  const Color& sp,
  const FontOptions font_opts,
  D2D1_POINT_2F top_left,
  D2D1_POINT_2F bot_right,
  float font_width,
  float font_height,
  ID2D1SolidColorBrush& fg_brush,
  IDWriteTextFormat* text_format,
  bool clip
)
{
  if (clip)
  {
    target.PushAxisAlignedClip({
      top_left.x, top_left.y, bot_right.x, bot_right.y
    }, D2D1_ANTIALIAS_MODE_ALIASED);
  }
  fg_brush.SetColor(d2color(fg.to_uint32()));
  using key_type = decltype(layout_cache)::key_type;
  IDWriteTextLayout* old_text_layout = nullptr;
  IDWriteTextLayout1* text_layout = nullptr;
  key_type key = {text, font_opts};
  auto objptr = layout_cache.get(key);
  if (objptr) text_layout = *objptr;
  else
  {
    HRESULT hr;
    auto* factory = editor_area->dwrite_factory();
    hr = factory->CreateTextLayout(
      (LPCWSTR) text.constData(),
      text.size(),
      text_format,
      // Sometimes the text clips weirdly & adding
      // to the width solves it. It's probably because
      // the text is just a little wider than the max width we set
      // so we increase the max width by a little here.
      bot_right.x - top_left.x + 1000.f,
      bot_right.y - top_left.y,
      &old_text_layout
    );
    if (FAILED(hr)) return;
    // IDWriteTextLayout1 can set char spacing
    hr = old_text_layout->QueryInterface(&text_layout);
    if (FAILED(hr)) { SafeRelease(&old_text_layout); return; }
    DWRITE_TEXT_RANGE text_range {0, (UINT32) text.size()};
    auto charspace = editor_area->charspacing();
    if (charspace)
    {
      text_layout->SetCharacterSpacing(0, float(charspace), 0, text_range);
    }
    if (font_opts & FontOpts::Italic)
    {
      text_layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, text_range);
    }
    if (font_opts & FontOpts::Bold)
    {
      text_layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, text_range);
    }
    layout_cache.put(key, text_layout);
  }
  auto offset = float(editor_area->linespacing()) / 2.f;
  D2D1_POINT_2F text_pt = {top_left.x, top_left.y + offset};
  target.DrawTextLayout(
    text_pt,
    text_layout,
    &fg_brush,
    D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT
  );
  fg_brush.SetColor(D2D1::ColorF(sp.to_uint32()));
  SafeRelease(&old_text_layout);
  const auto draw_path = [&](const FontOpts fo) {
    draw_text_decorations(
      &target, fo, top_left, bot_right, font_width, font_height, fg_brush
    );
  };
  if (font_opts & FontOpts::Underline) draw_path(FontOpts::Underline);
  if (font_opts & FontOpts::Undercurl) draw_path(FontOpts::Undercurl);
  if (font_opts & FontOpts::Strikethrough) draw_path(FontOpts::Strikethrough);
  if (clip) target.PopAxisAlignedClip();
}

void D2DPaintGrid2::draw_bg(
  ID2D1RenderTarget& target,
  const Color& bg,
  D2D1_POINT_2F top_left,
  D2D1_POINT_2F bot_right,
  ID2D1SolidColorBrush& brush
)
{
  brush.SetColor(D2D1::ColorF(bg.to_uint32()));
  target.FillRectangle({top_left.x, top_left.y, bot_right.x, bot_right.y}, &brush);
}

void D2DPaintGrid2::draw_bg(
  ID2D1RenderTarget& target,
  const Color& bg,
  D2D1_RECT_F rect,
  ID2D1SolidColorBrush& brush
)
{
  draw_bg(target, bg, {rect.left, rect.top}, {rect.right, rect.bottom}, brush);
}

void D2DPaintGrid2::draw_text_and_bg(
  ID2D1RenderTarget* context,
  const QString& buf,
  const HLAttr& attr,
  const HLAttr& fallback,
  D2D1_POINT_2F start,
  D2D1_POINT_2F end,
  float font_width,
  float font_height,
  IDWriteTextFormat* text_format,
  ID2D1SolidColorBrush* fg_brush,
  ID2D1SolidColorBrush* bg_brush
)
{
  auto [fg, bg, sp] = attr.fg_bg_sp(fallback);
  auto bg_tl = D2D1::Point2F(std::floor(start.x), start.y);
  draw_bg(*context, bg, bg_tl, end, *bg_brush);
  draw_text(
    *context, buf, fg, sp, attr.font_opts, start, end,
    font_width, font_height, *fg_brush, text_format
  );
}

void D2DPaintGrid2::draw(
  ID2D1RenderTarget* context,
  QRect r,
  ID2D1SolidColorBrush* fg_brush,
  ID2D1SolidColorBrush* bg_brush
)
{
  auto target_size = context->GetSize();
  const auto& fonts = editor_area->fallback_list();
  //const int start_x = r.left(), end_x = r.right();
  const int start_y = r.top(), end_y = r.bottom();
  const auto font_dims = editor_area->font_dimensions();
  const float font_width = font_dims.width;
  const float font_height = font_dims.height;
  const HLState* s = &editor_area->hlstate();
  QString buffer;
  buffer.reserve(100);
  const auto& def_clrs = s->default_colors_get();
  u32 cur_font_idx = 0;
  const auto get_pos = [&](int x, int y, int num_chars) {
    d2pt tl = {x * font_width, y * font_height};
    d2pt br = {(x + num_chars) * font_width, (y + 1) * font_height};
    return std::pair {tl, br};
  };
  const auto draw_buf = [&](const HLAttr& main, d2pt start, d2pt end) {
    if (buffer.isEmpty()) return;
    const auto& tf = fonts[cur_font_idx];
    reverse_qstring(buffer);
    draw_text_and_bg(
      context, buffer, main, def_clrs, start, end, font_width, font_height,
      tf.Get(), fg_brush, bg_brush
    );
    buffer.resize(0);
  };
  for(int y = start_y; y <= end_y && y < rows; ++y)
  {
    d2pt end = {target_size.width, (y + 1) * font_height};
    int prev_hl_id = 0;
    if (y * cols + (cols - 1) < (int) area.size())
    {
      prev_hl_id = area[y * cols + cols - 1].hl_id;
    }
    /// Reverse iteration. This prevents text from clipping
    for(int x = cols - 1; x >= 0; --x)
    {
      const auto& gc = area[y * cols + x];
      const auto font_idx = editor_area->font_for_ucs(gc.ucs);
      /// Neovim double-width characters have an empty string after them.
      /// Iterating from right to left we see the empty string first,
      /// then the double width character, which is why we have to draw
      /// the buffer as soon as we see the empty string.
      if (gc.text.isEmpty())
      {
        const auto [tl, br] = get_pos(x + 1, y, 0);
        draw_buf(s->attr_for_id(prev_hl_id), tl, end);
        end = br;
      }
      if (font_idx != cur_font_idx && !gc.text[0].isSpace())
      {
        const auto [tl, br] = get_pos(x + 1, y, 0);
        draw_buf(s->attr_for_id(prev_hl_id), tl, end);
        end = br;
        cur_font_idx = font_idx;
      }
      if (gc.double_width)
      {
        // Assume previous text has already been drawn.
        const auto [tl, br] = get_pos(x, y, 2);
        buffer.append(gc.text);
        draw_buf(s->attr_for_id(gc.hl_id), tl, br);
        end = get_pos(x, y, 0).second;
        prev_hl_id = gc.hl_id;
      }
      else if (gc.hl_id == prev_hl_id)
      {
        buffer.append(gc.text);
        continue;
      }
      else
      {
        const auto [tl, br] = get_pos(x + 1, y, 0);
        draw_buf(s->attr_for_id(prev_hl_id), tl, end);
        end = br;
        buffer.append(gc.text);
        prev_hl_id = gc.hl_id;
      }
    }
    d2pt start = {0, y * font_height};
    draw_buf(s->attr_for_id(prev_hl_id), start, end);
  }
}

D2DPaintGrid2::d2pt D2DPaintGrid2::pos() const
{
  auto&& [font_width, font_height] = editor_area->font_dimensions();
  return {x * font_width, y * font_height};
}

D2DPaintGrid2::d2rect D2DPaintGrid2::rect() const
{
  auto size = bitmap->GetSize();
  int left = top_left.x();
  int top = top_left.y();
  float right = left + size.width;
  float bottom = top + size.height;
  return D2D1::RectF(left, top, right, bottom);
}

D2DPaintGrid2::d2rect D2DPaintGrid2::source_rect() const
{
  auto size = bitmap->GetSize();
  return D2D1::RectF(0, 0, size.width, size.height);
}

void D2DPaintGrid2::set_pos(u16 new_x, u16 new_y)
{
  if (!editor_area->animations_enabled() || is_float())
  {
    move_update_timer.stop();
    GridBase::set_pos(new_x, new_y);
    update_position(new_x, new_y);
    return;
  }
  old_move_x = cur_left;
  old_move_y = cur_top;
  dest_move_x = new_x;
  dest_move_y = new_y;
  move_animation_time = editor_area->move_animation_duration();
  auto interval = editor_area->move_animation_frametime();
  if (move_update_timer.interval() != interval)
  {
    move_update_timer.setInterval(interval);
  }
  GridBase::set_pos(new_x, new_y);
  if (!move_update_timer.isActive()) move_update_timer.start();
}

void D2DPaintGrid2::update_position(double x, double y)
{
  auto&& [font_width, font_height] = editor_area->font_dimensions();
  top_left = {x * font_width, y * font_height};
}

void D2DPaintGrid2::viewport_changed(Viewport vp)
{
  if (!modified) vp = viewport;
  // Thanks to Keith Simmons, the Neovide developer for explaining
  // his implementation of Neovide's smooth scrolling in his blog post
  // at http://02credits.com/blog/day96-neovide-smooth-scrolling.
  if (!editor_area->animations_enabled() || viewport.topline == vp.topline)
  {
    GridBase::viewport_changed(vp);
    modified = false;
    return;
  }
  auto dest_topline = vp.topline;
  start_scroll_y = current_scroll_y;
  dest_scroll_y = dest_topline;
  snapshots.push_back({viewport, copy_bitmap(bitmap.Get())});
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

ComPtr<ID2D1Bitmap1> D2DPaintGrid2::copy_bitmap(ID2D1Bitmap1* src)
{
  Q_UNUSED(src);
  auto size = src->GetPixelSize();
  ComPtr<ID2D1Bitmap1> dst = nullptr;
  render_target->CreateBitmap(
    size, nullptr, 0,
    D2D1::BitmapProperties1(
      D2D1_BITMAP_OPTIONS_TARGET,
      src->GetPixelFormat()
    ),
    &dst
  );
  auto tl = D2D1::Point2U(0, 0);
  auto src_rect = D2D1::RectU(0, 0, size.width, size.height);
  dst->CopyFromBitmap(&tl, src, &src_rect);
  return dst;
}

static const auto interp = D2D1_BITMAP_INTERPOLATION_MODE_LINEAR;

void D2DPaintGrid2::render(ID2D1DeviceContext* target)
{
  auto&& [font_width, font_height] = editor_area->font_dimensions();
  auto sz = bitmap->GetSize();
  d2rect r = rect();
  auto bg = editor_area->default_bg().to_uint32();
  auto tly = (int) top_left.y();
  auto tlx = (int) top_left.x();
  QRectF rect(tlx, tly, (qreal) sz.width, (qreal) sz.height);
  ComPtr<ID2D1SolidColorBrush> bg_brush = nullptr;
  target->CreateSolidColorBrush(D2D1::ColorF(bg), &bg_brush);
  // Sometimes in multigrid mode the root grid can 'peek through'
  // by 1 pixel, because of rounding error
  // Increase the width of the fill rectangle by 1 on each side
  // to correct this
  auto fill_rect = D2D1::RectF(r.left - 1, r.top, r.right + 1, r.bottom);
  target->FillRectangle(fill_rect, bg_brush.Get());
  if (!editor_area->animations_enabled() || !is_scrolling)
  {
    target->DrawBitmap(
      bitmap.Get(),
      &r,
      1.0f,
      interp
    );
    return;
  }
  target->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_ALIASED);
  float cur_scroll_y = current_scroll_y * font_height;
  float cur_snapshot_top = viewport.topline * font_height;
  for(auto it = snapshots.rbegin(); it != snapshots.rend(); ++it)
  {
    const auto& snapshot = *it;
    float snapshot_top = snapshot.vp.topline * font_height;
    float offset = snapshot_top - cur_scroll_y;
    auto pixmap_top = tly + offset;
    d2pt pt = D2D1::Point2F(tlx, pixmap_top);
    r = D2D1::RectF(pt.x, pt.y, pt.x + sz.width, pt.y + sz.height);
    target->DrawBitmap(
      snapshot.image.Get(),
      &r,
      1.0f,
      interp
    );
  }
  float offset = cur_snapshot_top - cur_scroll_y;
  d2pt pt = D2D1::Point2F(tlx, tly + offset);
  r = D2D1::RectF(pt.x, pt.y, pt.x + sz.width, pt.y + sz.height);
  target->DrawBitmap(
    bitmap.Get(),
    &r,
    1.0f,
    interp
  );
  target->PopAxisAlignedClip();
}

void D2DPaintGrid2::draw_cursor(ID2D1RenderTarget *target, const Cursor &cursor)
{
  const auto& text_formats = editor_area->fallback_list();
  const HLState* hl = &editor_area->hlstate();
  const auto [font_width, font_height] = editor_area->font_dimensions();
  const auto pos_opt = cursor.pos();
  if (!pos_opt) return;
  const auto pos = pos_opt.value();
  std::size_t idx = pos.row * cols + pos.col;
  if (idx >= area.size()) return;
  const auto& gc = area[idx];
  float scale_factor = 1.0f;
  if (gc.double_width) scale_factor = 2.0f;
  const CursorRect rect = *cursor.rect(font_width, font_height, scale_factor);
  ComPtr<ID2D1SolidColorBrush> brush = nullptr;
  const HLAttr& attr = hl->attr_for_id(rect.hl_id);
  auto [fg, bg, sp] = attr.fg_bg_sp(hl->default_colors_get());
  if (attr.hl_id == 0) std::swap(fg, bg);
  target->CreateSolidColorBrush(D2D1::ColorF(bg.to_uint32()), &brush);
  const QRectF& r = rect.rect;
  auto fill_rect = D2D1::RectF(r.left(), r.top(), r.right(), r.bottom());
  // Draw cursor background
  brush->SetOpacity(rect.opacity);
  draw_bg(*target, bg, fill_rect, *brush.Get());
  brush->SetOpacity(1.0f);
  if (rect.should_draw_text)
  {
    fill_rect.right = std::max(fill_rect.right, fill_rect.left + font_width);
    // If the rect exists, the pos must exist as well.
    auto font_idx = editor_area->font_for_ucs(gc.ucs);
    assert(font_idx < text_formats.size());
    FontOptions fo = attr.font_opts == FontOpts::Normal
      ? hl->attr_for_id(gc.hl_id).font_opts
      : attr.font_opts;
    const auto start = D2D1::Point2F((x + pos.col) * font_width, (y + pos.row) * font_height);
    const auto end = D2D1::Point2F(start.x + (scale_factor * font_width), start.y + font_height);
    draw_text(
      *target, gc.text, fg, sp, fo, start, end,
      font_width, font_height, *brush.Get(), text_formats[font_idx].Get(), true
    );
  }
}

void D2DPaintGrid2::scrolled(int top, int bot, int left, int right, int rows)
{
  auto [font_width, font_height] = editor_area->font_dimensions();
  auto dest_tl = D2D1::Point2U(left * font_width, (top - rows) * font_height);
  auto rect = D2D1::RectU(
    left * font_width,
    top * font_height,
    right * font_width,
    bot * font_height
  );
  auto cloned = copy_bitmap(bitmap.Get());
  bitmap->CopyFromBitmap(&dest_tl, cloned.Get(), &rect);
}

D2DPaintGrid2::~D2DPaintGrid2() = default;
