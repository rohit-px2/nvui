#ifndef NVUI_EDITOR_BASE_HPP
#define NVUI_EDITOR_BASE_HPP

#include <span>
#include <memory>
#include "cmdline.hpp"
#include "cursor.hpp"
#include "fontdesc.hpp"
#include "grid.hpp"
#include "hlstate.hpp"
#include "object.hpp"
#include "popupmenu.hpp"
#include "nvim.hpp"

/// UI Capabilities (Extensions)
struct ExtensionCapabilities
{
  bool linegrid = false;
  bool popupmenu = false;
  bool wildmenu = false;
  bool messages = false;
  bool cmdline = false;
  bool multigrid = false;
};

// Base class for Editor UIs.
// Doesn't assume any UI but uses Qt
// for the event loop, and utility classes like QPoint, QSize, QRect
// which are used in other classes like GridBase.
struct EditorBase
{
public:
  struct NvimDimensions
  {
    int width;
    int height;
  };
  using HandlerFunc = std::function<void (std::span<const Object>)>;
  EditorBase(
    std::string nvim_path,
    std::vector<std::string> nvim_args,
    QObject* thread_target_obj = qApp
  );
  // Sets up the EditorBase to handle Neovim events.
  // This also creates instances of the cmdline and popup menu
  // throught the virtual popup_new() and cmdline_new().
  // If this isn't called then redraw events won't be managed.
  virtual void setup();
  Color default_bg() const;
  Color default_fg() const;
  bool mouse_enabled() const;
  void nvim_ui_attach(
    int width, int height, std::unordered_map<std::string, bool> capabilities
  );
  const HLState& hlstate() const;
  FontDimensions font_dimensions() const;
  NvimDimensions nvim_dimensions() const;
  FontOpts default_font_weight() const;
  FontOpts default_font_style() const;
  // Send "confirm qa" signal to Neovim to close.
  void confirm_qa();
  bool nvim_exited() const;
  virtual ~EditorBase();
private:
  /**
   * Handles a Neovim "grid_resize" event.
   */
  void grid_resize(std::span<const Object> objs);
  /**
   * Handles a Neovim "grid_line" event.
   */
  void grid_line(std::span<const Object> objs);
  /**
   * Paints the grid cursor at the given grid, row, and column.
   */
  void grid_cursor_goto(std::span<const Object> objs);
  /**
   * Handles a Neovim "option_set" event.
   */
  void option_set(std::span<const Object> objs);
  /**
   * Handles a Neovim "flush" event.
   * This paints the internal buffer onto the window.
   */
  void flush();
  /**
   * Handles a Neovim "win_pos" event.
   */
  void win_pos(std::span<const Object> objs);
  /**
   * Handles a Neovim "win_hide" event.
   */
  void win_hide(std::span<const Object> objs);
  /**
   * Handles a Neovim "win_float_pos" event.
   */
  void win_float_pos(std::span<const Object> objs);
  /**
   * Handles a Neovim "win_close" event.
   */
  void win_close(std::span<const Object> objs);
  /**
   * Handles a Neovim "win_viewport" event.
   */
  void win_viewport(std::span<const Object> objs);
  /// Handles a Neovim "grid_destroy" event.
  void grid_destroy(std::span<const Object> objs);
  /// Handles a Neovim "msg_set_pos" event.
  void msg_set_pos(std::span<const Object> objs);
  /**
   * Handles a Neovim "grid_clear" event
   */
  void grid_clear(std::span<const Object> objs);
  /**
   * Handles a Neovim "grid_scroll" event
   */
  void grid_scroll(std::span<const Object> objs);
  /**
   * Notify the editor area when resizing is enabled/disabled.
   */
  void set_resizing(bool is_resizing);
  /**
   * Handles a "mode_info_set" Neovim redraw event.
   * Internally sends the data to neovim_cursor.
   */
  void mode_info_set(std::span<const Object> objs);
  /**
   * Handles a "mode_change" Neovim event.
   * Internally sends the data to neovim_cursor.
   */
  void mode_change(std::span<const Object> objs);
  /**
   * Handles a "busy_start" event, passing it to the Neovim cursor
   */
  inline void busy_start() { n_cursor.busy_start(); }
  /**
   * Handles a "busy_stop" event, passing it to the Neovim cursor
   */
  inline void busy_stop() { n_cursor.busy_stop(); }
  void cmdline_show(std::span<const Object> objs);
  void cmdline_hide(std::span<const Object> objs);
  void cmdline_cursor_pos(std::span<const Object> objs);
  void cmdline_special_char(std::span<const Object> objs);
  void cmdline_block_show(std::span<const Object> objs);
  void cmdline_block_append(std::span<const Object> objs);
  void cmdline_block_hide(std::span<const Object> objs);
  void popupmenu_show(std::span<const Object> objs);
  void popupmenu_hide(std::span<const Object> objs);
  void popupmenu_select(std::span<const Object> objs);
  void set_mouse_enabled(bool enabled);
private:
  virtual void do_close() = 0;
  // Inheritors will control the actual type of popup menu being created.
  // But the returned value must not be null.
  virtual std::unique_ptr<PopupMenu> popup_new() = 0;
  virtual std::unique_ptr<Cmdline> cmdline_new() = 0;
  virtual void cursor_moved() = 0;
  virtual void redraw() = 0;
  virtual void create_grid(u32 x, u32 y, u32 w, u32 h, u64 id);
  virtual void set_fonts(std::span<FontDesc> list) = 0;
  virtual void default_colors_changed(Color fg, Color bg) = 0;
  // When an "option_set" field was updated.
  // Some things like ui capabilities ("ext_*") are handled by the base
  // class but things like linespace need to be handled by UI inheritors
  virtual void field_updated(std::string_view field, const Object& value);
  void register_handlers();
  void handle_redraw(Object message);
  void order_grids();
  std::unordered_map<std::string, HandlerFunc> handlers;
  /**
   * Destroy the grid with the given grid_num.
   * If no grid exists with the given grid_num,
   * nothing happens.
   */
  void destroy_grid(u64 grid_num);
  i64 get_win(const NeovimExt& ext) const;
protected:
  void set_handler(
    std::string name,
    HandlerFunc func
  );
  GridBase* find_grid(i64 grid_num);
  void send_redraw();
  void screen_resized(int screenwidth, int screenheight);
  void set_font_dimensions(float width, float height);
protected:
  HLState hl_state;
  Cursor n_cursor;
  std::unique_ptr<PopupMenu> popup_menu;
  std::unique_ptr<Cmdline> cmdline;
  // This must get updated when changes are made to the dimensions
  // of the font
  std::vector<std::unique_ptr<GridBase>> grids;
  std::vector<FontDesc> guifonts;
  std::unique_ptr<Nvim> nvim;
  ExtensionCapabilities ext;
  bool grids_need_ordering = false;
  bool enable_mouse = false;
  bool done = false;
  FontDimensions ms_font_dimensions;
  std::string path_to_nvim;
  std::vector<std::string> args_to_nvim;
private:
  // Measures to prevent needless resizing requests
  QObject* target_object;
  QSize pixel_dimensions;
  QSize dimensions;
  std::optional<QSize> queued_resize;
  bool resizing;
  FontOpts default_weight = FontOpts::Normal;
  FontOpts default_style = FontOpts::Normal;
};

#endif // NVUI_EDITOR_BASE_HPP
