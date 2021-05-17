#ifndef NVUI_UTILS_HPP
#define NVUI_UTILS_HPP
#include <QIcon>
#include <QString>
#include <QFile>
#include <QSvgRenderer>
#include <QPainter>
#include <stdexcept>

// Creates a QIcon from the given svg, with the given foreground
// and background color.
// Assumption: The only color in the file is black ("#000").
inline QIcon icon_from_svg(
  const QString filename,
  const QColor& foreground,
  const QColor& background = QColor(0, 0, 0, 0)
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


#endif // NVUI_UTILS_HPP
