#ifndef NVUI_UTILS_HPP
#define NVUI_UTILS_HPP
#include <QCoreApplication>
#include <QIcon>
#include <QString>
#include <QFile>
#include <QStringBuilder>
#include <QSvgRenderer>
#include <QPainter>
#include <chrono>
#include <stdexcept>
#include <optional>

// Creates a QIcon from the given svg, with the given foreground
// and background color.
// Assumption: The only color in the file is black ("#000").
inline QIcon icon_from_svg(
  const QString filename,
  const QColor& foreground,
  const QColor& background = QColor(0, 0, 0, 0),
  int width = -1,
  int height = -1
)
{
  QFile file {filename};
  if (!file.exists())
  {
    throw std::runtime_error("file not found.");
  }
  file.open(QIODevice::ReadOnly);
  QString text = file.readAll();
  QSvgRenderer renderer {text.toUtf8()};
  if (width <= 0) width = renderer.defaultSize().width();
  if (height <= 0) height = renderer.defaultSize().height();
  QPixmap pm {width, height};
  pm.fill(background);
  QPainter painter(&pm);
  renderer.render(&painter);
  painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
  painter.fillRect(pm.rect(), foreground);
  return QIcon {pm};
}

inline std::optional<QPixmap> pixmap_from_svg(
  const QString& filename,
  const QColor& foreground,
  const QColor& background = Qt::transparent,
  int width = 0,
  int height = 0
)
{
  QFile file {filename};
  if (!file.exists()) return std::nullopt;
  file.open(QIODevice::ReadOnly);
  QString data = file.readAll();
  QSvgRenderer renderer {data.toUtf8()};
  QPixmap pm {width, height};
  if (pm.isNull()) return std::nullopt;
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  renderer.render(&p);
  p.setCompositionMode(QPainter::CompositionMode_SourceIn);
  p.fillRect(pm.rect(), foreground);
  if (background.alpha() == 0) return pm;
  p.end();
  QPixmap filled {width, height};
  filled.fill(background);
  QPainter painter(&filled);
  painter.drawPixmap(filled.rect(), pm, pm.rect());
  return filled;
}

// Macro to time how long something takes
// using std::chrono (only in debug mode)
#ifndef NDEBUG
#define TIME(expr) \
  using Clock = ::std::chrono::high_resolution_clock; \
  const auto start = Clock::now(); \
  (expr); \
  const auto end = Clock::now(); \
  ::std::cout << "Took " << ::std::chrono::duration<double, std::milli>(end - start).count() << " ms.\n";
#else
#define TIME(expr) (expr)
#endif // NDEBUG

inline QString normalize_path(const QString& path)
{
  return QCoreApplication::applicationDirPath() + "/" + path;
}

#endif // NVUI_UTILS_HPP
