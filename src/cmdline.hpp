#ifndef NVUI_CMDLINE_HPP
#define NVUI_CMDLINE_HPP
#include <QGraphicsDropShadowEffect>
#include <QObject>
#include <QWidget>
#include <QPixmap>
#include <msgpack.hpp>
#include <optional>
#include "hlstate.hpp"
#include "cursor.hpp"
#include "types.hpp"

class Nvim;

struct Cmdline
{
  Cmdline(const HLState& hl_state, const Cursor* cursor);
  virtual ~Cmdline() = default;
  virtual void register_nvim(Nvim&) = 0;
  void cmdline_show(std::span<const Object> objs);
  void cmdline_hide(std::span<const Object> objs);
  void cmdline_cursor_pos(std::span<const Object> objs);
  void cmdline_special_char(std::span<const Object> objs);
  void cmdline_block_show(std::span<const Object> objs);
  void cmdline_block_append(std::span<const Object> objs);
  void cmdline_block_hide(std::span<const Object> objs);
  void set_fg(Color);
  void set_bg(Color);
  void set_x(float left);
  void set_y(float top);
  void set_center_x(float x);
  void set_center_y(float y);
  void set_width(float width);
  void set_height(float height);
  void set_padding(u32 pad);
  bool hidden() const;
  virtual void editor_resized(int width, int height) = 0;
  virtual QRect get_rect() const = 0;
  virtual void set_font_family(std::string_view family) = 0;
  virtual void set_font_size(double point_size) = 0;
  void set_border_width(int pixels);
  void set_border_color(Color color);
  QString get_content_string() const;
protected:
  struct Chunk
  {
    int attr;
    QString text;
  };
  using Content = std::vector<Chunk>;
  const HLState& hl_state;
  const Cursor* p_cursor;
  std::optional<Color> inner_fg;
  std::optional<Color> inner_bg;
  std::optional<float> centered_x;
  std::optional<float> centered_y;
  Content content;
  std::vector<Content> block;
  float border_width = 1.f;
  Color border_color {0};
  std::optional<QString> first_char;
  // Rectangle relative to screen size of editor area.
  // Indicates (x, y, w, h). Height is ignored through since
  // the cmdline should adjust automatically.
  // Default puts the cmdline centered at the top of the window,
  // and taking up half the height of the editor area.
  QRectF rel_rect {0.25, 0, 0.5, 0.10};
  u32 padding = 1;
  // Before which character the cursor will show up on the current line.
  int cursor_pos = 0;
  bool is_hidden = true;
  int indent = 0;
private:
  QString complete_content_string;
protected:
  virtual void colors_changed(Color fg, Color bg) = 0;
  virtual void redraw() = 0;
  virtual void do_hide() = 0;
  // do_show() does not signal a redraw, it just says to make
  // the current content visible.
  // redraw() will be called before do_show()
  // when the cmdline is updated.
  virtual void do_show() = 0;
  virtual void border_changed() = 0;
  virtual void rect_changed(QRectF relative_rect) = 0;
private:
  void update_content_string();
  void convert_content(Content&, const ObjectArray&);
};

class CmdlineQ : public Cmdline, public QWidget
{
public:
  CmdlineQ(const HLState& hls, const Cursor* crs, QWidget* parent = nullptr);
  void register_nvim(Nvim&) override;
  QRect get_rect() const override;
  ~CmdlineQ() override;
protected:
  void do_hide() override;
  void do_show() override;
  void redraw() override;
  void colors_changed(Color fg, Color bg) override;
  void set_font_family(std::string_view family) override;
  void set_font_size(double point_size) override;
  void border_changed() override;
  void rect_changed(QRectF relative_rect) override;
  void paintEvent(QPaintEvent*) override;
private:
  void editor_resized(int width, int height) override;
  int num_lines(const Content&, const QFontMetricsF&) const;
  int fitting_height() const;
  void draw_cursor(QPainter&, const Cursor&);
  QFont cmd_font;
};

#endif
