#ifndef NVUI_GUI_HPP
#define NVUI_GUI_HPP

#include <QObject>
#include <QMainWindow>
#include <QWidget>
#include <QFont>
#include "nvim.hpp"
#include "hlstate.hpp"
#include <iostream>
#include <memory>
#include <functional>
#include <msgpack.hpp>
class Window;
// Passing in a Window * lets us avoid the overhead of a lambda capture
using obj_ref_cb = void (*)(Window *, const msgpack::object&);
/// The main window class which holds the rest of the GUI components.
/// Fundamentally, the Neovim area is just 1 big text box.
/// However, there are additional features that we are trying to
/// support.
class Window : public QMainWindow
{
public:
  Window(QWidget *parent = nullptr, std::shared_ptr<Nvim> nv = nullptr);
  void register_handlers();
  /**
   * Sets a handler for the corresponding function
   * in Neovim's redraw notification.
   */
  void set_handler(std::string method, obj_ref_cb handler);
public slots:
  /**
   * Handles a 'redraw' Neovim notification.
   * TODO: Decide parameter type and how this will be called
   * (signals etc.)
   */
  void handle_redraw(msgpack::object redraw_args);
private:
  HLState hl_state;
  std::shared_ptr<Nvim> nvim;
  std::unordered_map<std::string, obj_ref_cb> handlers;
  Q_OBJECT
};

#endif
