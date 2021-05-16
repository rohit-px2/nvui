#ifndef NVUI_TITLEBAR_HPP
#define NVUI_TITLEBAR_HPP

#include <QLayout>
#include <QWidget>
#include <QMainWindow>
#include <QHBoxLayout>
#include <QResizeEvent>
#include <QPointF>
#include <QEvent>
#include <QLabel>
#include <QPushButton>
class TitleBar
{
public:
  TitleBar(QString text, QMainWindow* window);
  /**
   * Sets the mutable text to text.
   */
  void set_text(QString text);
  /**
   * Sets foreground to color if foreground is true,
   * otherwise sets background to color.
   */
  void set_color(QColor color, bool is_foreground = true);
  /**
   * Sets the foreground and background color to bg and fg respectively.
   */
  void set_color(QColor fg, QColor bg);
  /** Get dimensions */
private:
  /**
   * Updates the titlebar with new colors.
   * This should only be called after the colors are updated
     (not when new text is set).
   */
  void update_titlebar();
  QString separator;
  bool maximized;
  QColor foreground;
  QColor background;
  QIcon close_icon;
  QIcon max_icon;
  QIcon min_icon;
  QPushButton* close_btn;
  QPushButton* max_btn;
  QPushButton* min_btn;
  QMainWindow* win;
  QFont title_font;
  QString constant_text;
  QString mutable_text;
  QLabel* label;
  QHBoxLayout* layout;
  QWidget* titlebar_widget;
};

#endif
