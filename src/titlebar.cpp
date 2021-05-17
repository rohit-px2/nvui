#include "titlebar.hpp"
#include "utils.hpp"
#include <QApplication>
#include <QObject>
#include <QPalette>
#include <iostream>
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
constexpr int tolerance = 10; // TODO Keep a constants file
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
  MenuButton(QWidget* parent = nullptr, QString style_sheet = "", QString hov = "")
    : QPushButton(parent),
      style(std::move(style_sheet)),
      hov_ss(std::move(hov))
  {
    setAutoFillBackground(true);
    setMouseTracking(true);
    installEventFilter(this);
    setStyleSheet(style);
  }
private:
  QString style;
  QString hov_ss;
  // At least on my system, the stylesheet's :hover property
  // doesn't detect hovers while the mouse isn't pressed.
  // The MenuButton solves this problem.
  // This is also probably pretty slow since setting the stylesheet
  // is a very roundabout way of setting the color
  void hover_move(QHoverEvent* event)
  {
    Q_UNUSED(event);
    if (cursor() == Qt::ArrowCursor)
    {
      setStyleSheet(hov_ss);
    }
    else
    {
      setStyleSheet(style);
    }
  }

  void hover_enter(QHoverEvent* event)
  {
    Q_UNUSED(event);
    if (cursor() == Qt::ArrowCursor)
    {
      setStyleSheet(hov_ss);
    }
  }

  void hover_leave(QHoverEvent* event)
  {
    Q_UNUSED(event);
    setStyleSheet(style);
  }

  void mouse_pressed(QMouseEvent* event)
  {
    Q_UNUSED(event);
    if (cursor() != Qt::ArrowCursor)
    {
      // Little hacky but it works.
      // window() object is actually our custom Window
      // object that contains a resize_or_move function
      QMetaObject::invokeMethod(
        window(), "resize_or_move", Qt::AutoConnection, 
        Q_ARG(const QPointF&, event->localPos())
      );
    }
    else
    {
      QPushButton::mousePressEvent(event);
    }
  }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override
  {
    Q_UNUSED(watched);
    switch(event->type())
    {
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
};

/**
  TitleBar implementation
*/

constexpr QLatin1String make_string(const char* const str)
{
  return QLatin1String(str, static_cast<int>(std::char_traits<char>::length(str)));
}

constexpr QLatin1String min_max_ss_dark = make_string(R"(
QPushButton:hover
{
  background-color: #665c74;
  border: 2px solid #665c74;
}
)");

constexpr QLatin1String min_max_ss_light = make_string(R"(
QPushButton:hover
{
  background-color: #afafaf;
  border: 2px solid #afafaf;
}
)");

constexpr QLatin1String close_ss = make_string(R"(
QPushButton:hover
{
  background-color: red;
  border: 2px solid red;
}
)");

constexpr QLatin1String close_hov_bg = make_string("#ff0000");
constexpr QLatin1String mm_hov_light = make_string("#665c74");
constexpr QLatin1String mm_hov_dark = make_string("#afafaf");
constexpr QLatin1String default_ss = make_string("border: 2px solid transparent;");

TitleBar::TitleBar(QString text, QMainWindow* window)
: separator(" "),
  maximized(false),
  foreground("#ffffff"),
  background("#282c34"),
  win(window),
  left_text(std::move(text)),
  right_text("")
{
  title_font.setPointSize(11);
  title_font.setHintingPreference(QFont::HintingPreference::PreferFullHinting);
  title_font.setStyleStrategy(QFont::PreferAntialias);
  layout = new QHBoxLayout();
  QPushButton* appicon = new MenuButton(nullptr, default_ss, default_ss);
  appicon->setMouseTracking(true);
  appicon->setFlat(true);
  appicon->setIcon(QIcon("../assets/appicon.png"));
  layout->addWidget(appicon);
  label = new QLabel(left_text);
  label->setFont(title_font);
  layout->addStretch();
  layout->setMargin(0);
  layout->addWidget(label);
  // Create Icons (when we maximize the window we will update max_btn so it's a little special)
  close_icon = icon_from_svg("../assets/close-windows.svg", foreground);
  max_icon = icon_from_svg("../assets/max-windows.svg", foreground);
  min_icon = icon_from_svg("../assets/min-windows.svg", foreground);
  close_btn = new MenuButton(nullptr, default_ss, close_ss);
  max_btn = new MenuButton(nullptr, default_ss, min_max_ss_dark);
  min_btn = new MenuButton(nullptr, default_ss, min_max_ss_dark);
  //close_btn = new QPushButton();
  //max_btn = new QPushButton();
  //min_btn = new QPushButton();
  close_btn->setIcon(close_icon);
  close_btn->setFlat(true);
  max_btn->setIcon(max_icon);
  max_btn->setFlat(true);
  min_btn->setFlat(true);
  //close_btn->setStyleSheet(close_ss);
  //min_btn->setStyleSheet(min_max_ss_dark);
  //max_btn->setStyleSheet(min_max_ss_light);
  QObject::connect(close_btn, SIGNAL(clicked()), win, SLOT(close()));
  QObject::connect(min_btn, SIGNAL(clicked()), win, SLOT(showMinimized()));
  QObject::connect(max_btn, SIGNAL(clicked()), this, SLOT(minimize_maximize()));
  min_btn->setIcon(min_icon);
  layout->addStretch();
  // If only we could set mouse tracking to true by default...
  close_btn->setMouseTracking(true);
  min_btn->setMouseTracking(true);
  max_btn->setMouseTracking(true);
  layout->addWidget(min_btn);
  layout->addWidget(max_btn);
  layout->addWidget(close_btn);
  titlebar_widget = new QWidget();
  titlebar_widget->setLayout(layout);
  titlebar_widget->setStyleSheet("background-color: " + background.name() + "; color: " + foreground.name() + ";");
  titlebar_widget->setMouseTracking(true);
  win->setMenuWidget(titlebar_widget);
  const int menu_height = qApp->screens()[0]->size().height() / RATIO;
  const int menu_width = (menu_height * 3) / 2;
  std::cout << "Menu height: " << menu_height << '\n';
  const QSize size = QSize(menu_width, menu_height);
  close_btn->setFixedSize(size);
  min_btn->setFixedSize(size);
  max_btn->setFixedSize(size);
}

void TitleBar::update_text()
{
  // No point in having separator if there's no text on the right side
  if (!right_text.isEmpty())
  {
    label->setText(left_text + separator + right_text);
  }
  else
  {
    label->setText(left_text);
  }
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

void TitleBar::update_titlebar()
{
  close_icon = icon_from_svg("../assets/close-windows.svg", foreground);
  min_icon = icon_from_svg("../assets/min-windows.svg", foreground);
  close_btn->setIcon(close_icon);
  min_btn->setIcon(min_icon);
  update_maxicon();
  const QString ss = "background: " + background.name() + "; color: " + foreground.name() + ";";
  titlebar_widget->setStyleSheet(ss);
}

void TitleBar::update_maxicon()
{
  if (win->isMaximized())
  {
    max_icon = icon_from_svg("../assets/max-windows-second.svg", foreground);
    max_btn->setIcon(max_icon);
  }
  else
  {
    max_icon = icon_from_svg("../assets/max-windows.svg", foreground);
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
    update_maxicon();
  }
  else
  {
    win->showMaximized();
    update_maxicon();
  }
}
