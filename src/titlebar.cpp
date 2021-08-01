#include "titlebar.hpp"
#include "utils.hpp"
#include <cassert>
#include <QApplication>
#include <QObject>
#include <QPalette>
#include <iostream>
#include <QLayoutItem>
#include <stdexcept>
#include <string>
#include <utility>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QColor>
#include <QRect>
#include <QScreen>
#include <QSize>
#include <QWindow>
#include <QtCore/QStringBuilder>
#include "constants.hpp"
#include "window.hpp"

constexpr int RATIO = 36; // I think the height of menu bar is 30px on 1080p screen

/// MenuButton class is a wrapper around QPushButton
/// that is made for titlebar buttons. This means that
/// for example, if the button is at a corner of the screen,
/// you are still able to resize the window without clicking the
/// button. Of course, you can't move it.
/// Also uses hover events to color the button background, since
/// stylesheets didn't seem to be working (when the mouse wasn't pressed).
class MenuButton : public QPushButton
{
public:
  MenuButton(QWidget* parent = nullptr, QColor hov = Qt::transparent)
    : QPushButton(parent),
      hov_bg(hov)
  {
    setFocusPolicy(Qt::NoFocus);
    setFlat(true);
    setAutoFillBackground(true);
    setMouseTracking(true);
    installEventFilter(this);
  }

  MenuButton(TitleBar* parent, QColor hov = Qt::transparent)
    : MenuButton(static_cast<QWidget*>(nullptr), hov)
  {
    resize_move_handler = [parent](QPointF p) { emit parent->resize_move(p); };
  }

  void set_hov_bg(const QColor& bg)
  {
    hov_bg = bg;
  }
  
  // We shouldn't need to use this (can just change the parent widget),
  // but it's here
  void set_default_bg(const QColor& bg)
  {
    default_bg = bg;
  }

signals:
  void resize_move(QPointF pt);
private:
  std::function<void (QPointF)> resize_move_handler = [](auto){};
  QColor hov_bg;
  QColor default_bg = Qt::transparent;
  QColor cur_bg = default_bg;

  // At least on my system, the stylesheet's :hover property
  // doesn't detect hovers while the mouse isn't pressed.
  // The MenuButton solves this problem.
  // We manually check for hovers / mouse presses and set the color
  void hover_move(QHoverEvent* event)
  {
    Q_UNUSED(event);
    if (cursor() == Qt::ArrowCursor)
    {
      cur_bg = hov_bg;
    }
    else
    {
      cur_bg = default_bg;
    }
    update();
  }

  void hover_enter(QHoverEvent* event)
  {
    Q_UNUSED(event);
    if (cursor() == Qt::ArrowCursor)
    {
      cur_bg = hov_bg;
    }
    update();
  }

  void hover_leave(QHoverEvent* event)
  {
    Q_UNUSED(event);
    cur_bg = default_bg;
    update();
  }

  void mouse_pressed(QMouseEvent* event)
  {
    Q_UNUSED(event);
    if (cursor() != Qt::ArrowCursor)
    {
      resize_move_handler(event->windowPos());
    }
    else
    {
      QPushButton::mousePressEvent(event);
    }
  }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override
  {
    event->accept();
    Q_UNUSED(watched);
    switch(event->type())
    {
      case QEvent::Paint:
      {
        paintEvent(static_cast<QPaintEvent*>(event));
        return true;
      }
      case QEvent::HoverMove:
      {
        hover_move(static_cast<QHoverEvent*>(event));
        return true;
      }
      case QEvent::HoverEnter:
      {
        hover_enter(static_cast<QHoverEvent*>(event));
        return true;
      }
      case QEvent::HoverLeave:
      {
        hover_leave(static_cast<QHoverEvent*>(event));
        return true;
      }
      case QEvent::MouseButtonPress:
      {
        mouse_pressed(static_cast<QMouseEvent*>(event));
        return true;
      }
      default:
      {
        QPushButton::event(event);
        return true;
      }
    }
    return false;
  }
  void paintEvent(QPaintEvent* event) override
  {
    QPainter painter(this);
    painter.fillRect(rect(), cur_bg);
    QPushButton::paintEvent(event);
  }
};

/**
  TitleBar implementation
*/

// Hover colors of min, max, close buttons
static const QColor mm_light = "#665c74";
static const QColor mm_dark = "#3d4148";
static const QColor close_bg = {255, 0, 0}; // completely red

TitleBar::TitleBar(QString text, Window* window)
: separator(" "),
  maximized(false),
  foreground("#ffffff"),
  background("#282c34"),
  win(window),
  left_text(std::move(text)),
  right_text("")
{
  setMouseTracking(true);
  assert(qApp->screens().size() > 0);
  const int menu_height = qApp->screens()[0]->size().height() / RATIO;
  const int menu_width = (menu_height * 3) / 2;
  title_font.setPointSizeF(9.5);
  title_font.setHintingPreference(QFont::HintingPreference::PreferVerticalHinting);
  close_icon = icon_from_svg(constants::closeicon(), foreground);
  max_icon = icon_from_svg(constants::maxicon(), foreground);
  min_icon = icon_from_svg(constants::minicon(), foreground);
  close_btn = new MenuButton(this, close_bg);
  max_btn = new MenuButton(this, mm_dark);
  min_btn = new MenuButton(this, mm_dark);
  close_btn->setIcon(close_icon);
  max_btn->setIcon(max_icon);
  min_btn->setIcon(min_icon);
  layout = new QHBoxLayout();
  QPushButton* appicon = new MenuButton();
  appicon->setIcon(QIcon(constants::appicon()));
  label = new QLabel(left_text);
  label->setMouseTracking(true);
  label->setFont(title_font);
  // Window buttons on left for non-Windows
#if !defined(Q_OS_WIN)
  layout->addWidget(close_btn);
  layout->addWidget(min_btn);
  layout->addWidget(max_btn);
#else
  layout->addWidget(appicon);
  layout->addSpacerItem(new QSpacerItem(2 * menu_width, menu_height));
#endif
  layout->addStretch();
  layout->setMargin(0);
  layout->addWidget(label);
  // Create Icons (when we maximize the window we will update max_btn so it's a little special)
  layout->addStretch();
  // If only we could set mouse tracking to true by default...
#if defined(Q_OS_WIN)
  layout->addWidget(min_btn);
  layout->addWidget(max_btn);
  layout->addWidget(close_btn);
#else
  layout->addSpacerItem(new QSpacerItem(3 * menu_width, menu_height));
#endif
  titlebar_widget = new QWidget();
  titlebar_widget->setLayout(layout);
  titlebar_widget->setStyleSheet("background-color: " % background.name() % "; color: " % foreground.name() % ";");
  win->setMenuWidget(titlebar_widget);
  const QSize size {menu_width, menu_height};
  close_btn->setFixedSize(size);
  min_btn->setFixedSize(size);
  max_btn->setFixedSize(size);
  titlebar_widget->setFocusPolicy(Qt::NoFocus);
  setFocusPolicy(Qt::NoFocus);
  titlebar_widget->setMouseTracking(true);
  QObject::connect(close_btn, SIGNAL(clicked()), win, SLOT(close()));
  QObject::connect(min_btn, SIGNAL(clicked()), win, SLOT(showMinimized()));
  QObject::connect(max_btn, SIGNAL(clicked()), this, SLOT(minimize_maximize()));
}

void TitleBar::update_text()
{
  if (left_text.isEmpty() && right_text.isEmpty())
  {
    label->setText(middle_text);
  }
  else if (left_text.isEmpty() && middle_text.isEmpty())
  {
    label->setText(right_text);
  }
  else if (right_text.isEmpty() && middle_text.isEmpty())
  {
    label->setText(left_text);
  }
  else if (left_text.isEmpty())
  {
    label->setText(middle_text % separator % right_text);
  }
  else if (middle_text.isEmpty())
  {
    label->setText(left_text % separator % right_text);
  }
  else if (right_text.isEmpty())
  {
    label->setText(left_text % separator % middle_text);
  }
  else
  {
    label->setText(left_text % separator % middle_text % separator % right_text);
  }
  win->setWindowTitle(label->text());
}

void TitleBar::set_right_text(QString text)
{
  right_text = std::move(text);
  update_text();
}

void TitleBar::set_left_text(QString text)
{
  left_text = std::move(text);
  update_text();
}

void TitleBar::set_middle_text(QString text)
{
  middle_text = std::move(text);
  update_text();
}
bool is_light(const QColor& color)
{
  return color.lightness() >= 127;
}

void TitleBar::update_titlebar()
{
  close_icon = icon_from_svg(constants::closeicon(), foreground);
  min_icon = icon_from_svg(constants::minicon(), foreground);
  close_btn->setIcon(close_icon);
  min_btn->setIcon(min_icon);
  update_maxicon();
  const QString ss = "background: " % background.name() % "; color: " % foreground.name() % ";";
  titlebar_widget->setStyleSheet(ss);
  // Check the "mode" of the background color
  if (is_light(background))
  {
    min_btn->set_hov_bg(mm_light);
    max_btn->set_hov_bg(mm_light);
  }
  else
  {
    min_btn->set_hov_bg(mm_dark);
    max_btn->set_hov_bg(mm_dark);
  }
}

void TitleBar::update_maxicon()
{
  if (win->isMaximized())
  {
    max_icon = icon_from_svg(constants::maxicon_second(), foreground);
    max_btn->setIcon(max_icon);
  }
  else
  {
    max_icon = icon_from_svg(constants::maxicon(), foreground);
    max_btn->setIcon(max_icon);
  }
}

void TitleBar::set_color(QColor color, bool is_foreground)
{
  if (is_foreground)
  {
    foreground = std::move(color);
  }
  else
  {
    background = std::move(color);
  }
  update_titlebar();
}

void TitleBar::set_color(QColor fg, QColor bg)
{
  foreground = std::move(fg);
  background = std::move(bg);
  update_titlebar();
}

void TitleBar::set_separator(QString new_sep)
{
  separator = std::move(new_sep);
  update_text();
}

void TitleBar::minimize_maximize()
{
  if (win->isMaximized())
  {
    win->showNormal();
  }
  else
  {
    win->maximize();
  }
  update_maxicon();
}

void TitleBar::colors_changed(QColor fg, QColor bg)
{
  set_color(fg, bg);
}

void TitleBar::win_state_changed(Qt::WindowStates state)
{
  update_maxicon();
}
