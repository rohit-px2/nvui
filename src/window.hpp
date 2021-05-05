#ifndef NVUI_GUI_HPP
#define NVUI_GUI_HPP

#include <QApplication>
#include <QWidget>
#include <QFont>
#include "nvim.hpp"

/// The main window class which holds the rest of the GUI components.
/// Fundamentally, the Neovim area is just 1 big text box.
/// However, there are additional features that we are trying to
/// support.
class Window : public QApplication
{
public:
  /**
   * Handles a 'redraw' Neovim notification, with 'event'
   * being the parameters given in the message.
   */
  void HandleRedraw(const msgpack::object& event);
private:
  Q_OBJECT
};

#endif
