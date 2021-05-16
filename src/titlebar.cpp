#include "titlebar.hpp"
#include <QPushButton>
#include <QApplication>
#include <QObject>
#include <QStyle>
#include <QStyleFactory>
#include <stdexcept>
#include <utility>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QColor>
#include <QRect>
#include <QSvgRenderer>

// Creates a QIcon from the given svg, with the given foreground color.
// Assumption: The only color in the file is black ("#000").
static QIcon icon_from_svg(
  const QString filename,
  const QColor& foreground,
  const QColor& background
)
{
  QFile file {filename};
  if (!file.exists())
  {
    throw std::runtime_error("file not found.");
  }
  file.open(QIODevice::ReadOnly);
  QString text = file.readAll();
  QString to_replace = "\"" + foreground.name() + "\"";
  text.replace(R"("#000")", to_replace);
  QSvgRenderer renderer {text.toUtf8()};
  QPixmap pm {renderer.defaultSize()};
  pm.fill(background);
  QPainter painter(&pm);
  renderer.render(&painter);
  return QIcon {pm};
}

TitleBar::TitleBar(QString text, QMainWindow* window)
: separator(" "),
  maximized(false),
  foreground("#abb2bf"),
  background("#282c34"),
  win(window),
  constant_text(std::move(text)),
  mutable_text("")
{
  title_font.setPointSizeF(11.25);
  title_font.setHintingPreference(QFont::HintingPreference::PreferFullHinting);
  title_font.setStyleStrategy(QFont::PreferBitmap);
  layout = new QHBoxLayout();
  label = new QLabel(constant_text);
  label->setFont(title_font);
  layout->addStretch();
  layout->addWidget(label);
  // Create Icons (when we maximize the window we will update max_btn so it's a little special)
  close_icon = icon_from_svg("../assets/close-windows.svg", foreground, background);
  max_icon = icon_from_svg("../assets/max-windows.svg", foreground, background);
  min_icon = icon_from_svg("../assets/min-windows.svg", foreground, background);
  QPushButton* close_btn = new QPushButton();
  close_btn->setIcon(close_icon);
  close_btn->setFlat(true);
  QPushButton* max_btn = new QPushButton();
  max_btn->setIcon(max_icon);
  max_btn->setFlat(true);
  QPushButton* min_btn = new QPushButton();
  min_btn->setFlat(true);
  QObject::connect(close_btn, SIGNAL(clicked()), win, SLOT(close()));
  QObject::connect(min_btn, SIGNAL(clicked()), win, SLOT(showMinimized()));
  QObject::connect(max_btn, SIGNAL(clicked()), win, SLOT(showMaximized()));
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
  titlebar_widget->setStyleSheet("background-color: #282c34; color: #abb2bf;");
  win->setMenuWidget(titlebar_widget);
  titlebar_widget->setMouseTracking(true);
}

void TitleBar::set_text(QString text)
{
  mutable_text = std::move(text);
  label->setText(constant_text + separator + mutable_text);
}

void TitleBar::update_titlebar()
{
  close_icon = icon_from_svg("../assets/close-windows.svg", foreground, background);
  min_icon = icon_from_svg("../assets/min-windows.svg", foreground, background);
  max_icon = icon_from_svg("../assets/max-windows.svg", foreground, background);
  close_btn->setIcon(close_icon);
  min_btn->setIcon(min_icon);
  max_btn->setIcon(max_icon);
  const QString ss = "background: " + background.name() + "; color: " + foreground.name() + ";";
  titlebar_widget->setStyleSheet(ss);
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
