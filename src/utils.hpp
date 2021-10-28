#ifndef NVUI_UTILS_HPP
#define NVUI_UTILS_HPP
#include <QCoreApplication>
#include <QIcon>
#include <QString>
#include <QFile>
#include <QStringBuilder>
#include <QSvgRenderer>
#include <QPainter>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <optional>
#include <queue>
#include <msgpack.hpp>

// Creates a QIcon from the given svg, with the given foreground
// and background color.
// Assumption: The only color in the file is black ("#000").
inline std::optional<QIcon> icon_from_svg(
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
    return std::nullopt;
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

/// Resizes the 1d vector with size (prev_rows * prev_cols) to the
/// size (rows * cols) in the same way that a 2d vector would be resized.
/// This means that if cols < prev_cols, for example, the extra columns
/// are cut off from the right.
/// I think this is needed for grid resizing.
template<typename T, typename Size_Type = typename std::vector<T>::size_type>
void resize_1d_vector(
  std::vector<T>& v,
  Size_Type cols,
  Size_Type rows,
  Size_Type prev_cols,
  Size_Type prev_rows,
  T default_obj = {}
)
{
  if (v.size() / prev_cols != prev_rows) return;
  std::vector<T> new_v;
  new_v.resize(cols * rows, default_obj);
  for(int i = 0; i < std::min(rows, prev_rows); ++i)
  {
    for(int j = 0; j < std::min(cols, prev_cols); ++j)
    {
      new_v[i * cols + j] = v[i * prev_cols + j];
    }
  }
  v.swap(new_v);
}

template<typename T>
msgpack::object_handle pack(const T& obj)
{
  msgpack::sbuffer sbuf;
  msgpack::pack(sbuf, obj);
  return msgpack::unpack(sbuf.data(), sbuf.size());
}

/// Thanks Neovim-Qt
/// https://github.com/equalsraf/neovim-qt/blob/master/src/gui/shellwidget/shellwidget.cpp#L42-L50
/// Returns a monospace font family.
inline QString default_font_family()
{
#if defined(Q_OS_MAC)
  return "Courier New";
#elif defined(Q_OS_WIN)
  return "Consolas";
#else
  return "Monospace";
#endif
}

template<typename T>
void wait_for_value(std::atomic<T>& v, T val)
{
  while(v != val) {}
}

// find or default for map containers
template<typename Map, typename K, typename V>
V find_or_default(const Map& m, const K& k, const V& v)
{
  auto it = m.find(k);
  if (it == m.end()) return v;
  return it->second;
}

template<std::size_t idx = 0, typename... Types, typename Func>
void for_each_in_tuple(std::tuple<Types...>& t, Func&& f)
{
  constexpr auto s = std::integral_constant<std::size_t, idx> {};
  f(std::get<s>(t));
  if constexpr(idx != sizeof...(Types) - 1)
  {
    for_each_in_tuple<idx + 1>(t, std::forward<Func>(f));

/// UCS2-aware string reversal
inline void reverse_qstring(QString& s)
{
  const int len = s.size();
  for(int i = 0; i < len / 2; ++i)
  {
    if (s.at(i).isHighSurrogate())
    {
      QChar hi = s[i];
      QChar low = s[i + 1];
      s[i] = s[len - i - 2];
      s[i + 1] = s[len - i - 1];
      s[len - i - 2] = hi;
      s[len - i - 1] = low;
      i += 2;
    }
    else
    {
      QChar temp = s[i];
      s[i] = s[len - i - 1];
      s[len - i - 1] = temp;
    }
  }
}

#endif // NVUI_UTILS_HPP
