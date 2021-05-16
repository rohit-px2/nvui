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
  /** Get dimensions */
  int x() const;
  int y() const;
  int width() const;
  int height() const;
  QRect rect() const;
private:
  QString separator;
  bool maximized;
  QColor foreground;
  QColor background;
  QIcon close_icon;
  QIcon max_icon;
  QIcon min_icon;
  QPushButton* close_btn;
  QPushButton* max_btn;
  QPushButton min_btn;
  QMainWindow* win;
  QFont title_font;
  QString constant_text;
  QString mutable_text;
  QLabel* label;
  QHBoxLayout* layout;
  QWidget* titlebar_widget;
};

#endif
