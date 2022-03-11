#include "qeditor.hpp"
#include "qpaintgrid.hpp"
#include "font.hpp"
#include <QApplication>
#include <QEvent>

/**
 * Sets relative's point size to a point size that is such that the horizontal
 * advance of the character 'a' is within tolerance of the target's horizontal
 * advance for the same character.
 * This is done using a binary search algorithm between
 * (0, target.pointSizeF() * 2.). The algorithm runs in a loop, the number
 * of times can be limited using max_iterations. If max_iterations is 0
 * the loop will run without stopping until it is within error.
 */
static void set_relative_font_size(
  const QFont& target,
  QFont& modified,
  const double tolerance,
  const std::size_t max_iterations
)
{ 
  constexpr auto width = [](const QFontMetricsF& m) {
    return m.horizontalAdvance('a');
  };
  QFontMetricsF target_metrics {target};
  const double target_width = width(target_metrics);
  double low = 0.;
  double high = target.pointSizeF() * 2.;
  modified.setPointSizeF(high);
  for(u32 rep = 0;
      (rep < max_iterations || max_iterations == 0) && low <= high;
      ++rep)
  {
    double mid = (low + high) / 2.;
    modified.setPointSizeF(mid);
    QFontMetricsF metrics {modified};
    const double diff =  target_width - width(metrics);
    if (std::abs(diff) <= tolerance) return;
    if (diff < 0) /** point size too big */ high = mid;
    else if (diff > 0) /** point size too low */ low = mid;
    else return;
  }
}

QEditor::QEditor(
  int cols,
  int rows,
  std::unordered_map<std::string, bool> capabilities,
  std::string nvim_path,
  std::vector<std::string> nvim_args,
  QWidget* parent
)
: QWidget(parent),
  QtEditorUIBase(*this, cols, rows, std::move(capabilities),
  std::move(nvim_path), std::move(nvim_args))
{
  first_font.setFamily(default_font_family());
  first_font.setPointSizeF(11.25);
  setAttribute(Qt::WA_InputMethodEnabled);
  setAttribute(Qt::WA_OpaquePaintEvent);
  setAutoFillBackground(false);
  setAcceptDrops(true);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setFocusPolicy(Qt::StrongFocus);
  setFocus();
  setMouseTracking(true);
}

QEditor::~QEditor() = default;

void QEditor::setup()
{
  Base::setup();
}

std::unique_ptr<PopupMenu> QEditor::popup_new()
{
  return std::make_unique<PopupMenuQ>(&hl_state, this);
}

std::unique_ptr<Cmdline> QEditor::cmdline_new()
{
  return std::make_unique<CmdlineQ>(hl_state, &n_cursor, this);
}

void QEditor::resizeEvent(QResizeEvent* ev)
{
  Base::handle_nvim_resize(ev);
  update();
}

void QEditor::mousePressEvent(QMouseEvent* ev)
{
  Base::handle_mouse_press(ev);
  if (cursor() != Qt::ArrowCursor)
  {
    QWidget::mousePressEvent(ev);
  }
}

void QEditor::mouseReleaseEvent(QMouseEvent* ev)
{
  QWidget::mouseReleaseEvent(ev);
  Base::handle_mouse_release(ev);
}

void QEditor::wheelEvent(QWheelEvent* ev)
{
  Base::handle_wheel(ev);
}

void QEditor::dropEvent(QDropEvent* ev)
{
  Base::handle_drop(ev);
}

void QEditor::dragEnterEvent(QDragEnterEvent* ev)
{
  Base::handle_drag(ev);
}

void QEditor::inputMethodEvent(QInputMethodEvent* ev)
{
  Base::handle_ime_event(ev);
}

void QEditor::mouseMoveEvent(QMouseEvent* ev)
{
  Base::handle_mouse_move(ev);
  QWidget::mouseMoveEvent(ev);
}

void QEditor::keyPressEvent(QKeyEvent* ev)
{
  QWidget::keyPressEvent(ev);
  Base::handle_key_press(ev);
}

void QEditor::redraw() { update(); }

void QEditor::linespace_changed(float)
{
  update_font_metrics();
}

void QEditor::charspace_changed(float)
{
  update_font_metrics();
}

static void set_fontdesc(QFont& font, const FontDesc& fdesc)
{
  const auto& [name, size, opts] = fdesc;
  if (size > 0.f) font.setPointSizeF(size);
  font::set_opts(font, opts);
  QString fm = name.empty() ? default_font_family() : QString::fromStdString(name);
  font.setFamily(fm);
}

void QEditor::set_fonts(std::span<FontDesc> fontdescs)
{
  if (fontdescs.empty()) return;
  fallback_indices.clear();
  fonts.clear();
  QFontDatabase font_db;
  set_fontdesc(first_font, fontdescs.front());
  const auto validate_family = [&](QFont& f) {
    if (!font_db.hasFamily(f.family()))
    {
      nvim->err_write(fmt::format("No font named '{}' found.\n",
        f.family().toStdString()
      ));
      f.setFamily(default_font_family());
    }
  };
  validate_family(first_font);
  for(const auto& fontdesc : fontdescs)
  {
    QFont f;
    set_fontdesc(f, fontdesc);
    validate_family(f);
    set_relative_font_size(first_font, f, 0.0001, 1000);
    f.setWeight(qfont_weight(default_font_weight()));
    f.setStyle(qfont_style(default_font_style()));
    Font fo = f;
    fonts.push_back(std::move(f));
  }
  update_font_metrics();
}

void QEditor::update_font_metrics()
{
  first_font.setLetterSpacing(QFont::AbsoluteSpacing, charspace);
  QFontMetricsF metrics {first_font};
  float combined_height = std::max(metrics.height(), metrics.lineSpacing());
  double font_height = combined_height + linespacing();
  constexpr QChar any_char = 'W';
  double font_width = metrics.horizontalAdvance(any_char) + charspace;
  for(auto& f : fonts)
  {
    QFont old_font = f.font();
    old_font.setLetterSpacing(QFont::AbsoluteSpacing, charspace);
    f = old_font;
  }
  set_font_dimensions(font_width, font_height);
  // This is safe because we created an instance of PopupMenuQ
  // in the popup_new() function
  auto* popup = static_cast<PopupMenuQ*>(popup_menu.get());
  popup->font_changed(first_font, font_dimensions());
  screen_resized(width(), height());
  emit font_changed();
}

void QEditor::create_grid(u32 x, u32 y, u32 w, u32 h, u64 id)
{
  grids.push_back(std::make_unique<QPaintGrid>(this, x, y, w, h, id));
}

void QEditor::paintEvent(QPaintEvent*)
{
  QPainter p(this);
  p.fillRect(rect(), hl_state.default_colors_get().bg().value_or(0).qcolor());
  auto [cols, rows] = nvim_dimensions();
  auto [font_width, font_height] = font_dimensions();
  QRectF grid_clip_rect(0, 0, cols * font_width, rows * font_height);
  p.setClipRect(grid_clip_rect);
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  for(auto& grid_base : grids)
  {
    auto* grid = static_cast<QPaintGrid*>(grid_base.get());
    if (!grid->hidden)
    {
      QSize size = grid->buffer().size();
      auto r = QRectF(grid->pos(), size).intersected(grid_clip_rect);
      p.setClipRect(r);
      grid->process_events();
      grid->render(p);
    }
  }
  p.setClipRect(rect());
  if (!n_cursor.hidden() && cmdline->hidden())
  {
    auto* grid = find_grid(n_cursor.grid_num());
    if (grid) static_cast<QPaintGrid*>(grid)->draw_cursor(p, n_cursor);
  }
}

u32 QEditor::font_for_ucs(u32 ucs)
{
  if (ucs < 256) return 0;
  auto it = fallback_indices.find(ucs);
  if (it != fallback_indices.end()) return it->second;
  auto index = calc_fallback_index(ucs);
  fallback_indices[ucs] = index;
  return index;
}

u32 QEditor::calc_fallback_index(u32 ucs)
{
  for(u32 i = 0; i < fonts.size(); ++i)
  {
    if (fonts[i].raw().supportsCharacter(ucs)) return i;
  }
  return 0;
}

void QEditor::focusInEvent(QFocusEvent* event)
{
  Base::handle_focusgained(event);
  QWidget::focusInEvent(event);
}

void QEditor::focusOutEvent(QFocusEvent* event)
{
  Base::handle_focuslost(event);
  QWidget::focusOutEvent(event);
}

QVariant QEditor::inputMethodQuery(Qt::InputMethodQuery q) const
{
  return Base::handle_ime_query(q);
}
