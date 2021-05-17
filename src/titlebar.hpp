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


class MenuButton;

class TitleBar : public QWidget
{
  Q_OBJECT
public:
  TitleBar(QString text, QMainWindow* window);
  /**
   * Sets the right-side text to text.
   */
  void set_right_text(QString text);
  /**
   * Sets the left text.
   */
  void set_left_text(QString text);
  /**
   * Sets foreground to color if foreground is true,
   * otherwise sets background to color.
   */
  void set_color(QColor color, bool is_foreground = true);
  /**
   * Sets the foreground and background color to bg and fg respectively.
   */
  void set_color(QColor fg, QColor bg);
  /**
   * Set the separator string, which is what separates left_text and
   * right_text
   */
  void set_separator(QString new_sep);
  /**
   * Update the icons based on the status of the attached window (win).
   */
  void update_maxicon();
private:
  /**
   * Updates the titlebar with new colors.
   * This should only be called after the colors are updated
     (not when new text is set).
   */
  void update_titlebar();
  /**
   * Updates the titlebar text to the current values of
   * left_text, separator, and right_text
   */
  void update_text();
  QString separator;
  bool maximized;
  QColor foreground;
  QColor background;
  QIcon close_icon;
  QIcon max_icon;
  QIcon min_icon;
  MenuButton* close_btn;
  MenuButton* max_btn;
  MenuButton* min_btn;
  //QPushButton* close_btn;
  //QPushButton* max_btn;
  //QPushButton* min_btn;
  QMainWindow* win;
  QFont title_font;
  QString left_text;
  QString right_text;
  QLabel* label;
  QHBoxLayout* layout;
  QWidget* titlebar_widget;
public slots:
  void minimize_maximize();
};
#endif
