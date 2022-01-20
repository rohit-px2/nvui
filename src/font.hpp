#ifndef NVUI_FONT_HPP
#define NVUI_FONT_HPP
#include <QFont>
#include <QFontMetrics>
#include <QRawFont>
#include "hlstate.hpp"

// Font class created with the expectation that it will be modified
// very rarely, but read very frequently
// Caches bold, italic, and bolditalic versions of fonts
// to reduce the amount of QFont cloning
// and updates metrics such as whether the font is monospace
class Font
{
public:
  const QFont& font() const { return font_; }
  const QRawFont& raw() const { return raw_; }
  Font(const QString& family): font_(family), raw_(QRawFont::fromFont(font_))
  {
    update();
  }
  Font(const QFont& font): font_(font), raw_(QRawFont::fromFont(font_))
  {
    update();
  }
  Font& operator=(const QFont& f)
  {
    font_ = f;
    raw_ = QRawFont::fromFont(font_);
    update();
    return *this;
  }
  Font& operator=(const QString& family)
  {
    font_.setFamily(family);
    raw_ = QRawFont::fromFont(font_);
    update();
    return *this;
  }
  const QFont& bold_italic_font() const { return bolditalic; }
  const QFont& bold_font() const { return bold; }
  const QFont& italic_font() const { return italic; }
  const QFont& font_for(FontOptions opts) const
  {
    if (opts & FontOpts::Italic && opts & FontOpts::Bold)
    {
      return bolditalic;
    }
    else if (opts & FontOpts::Bold) return bold;
    else if (opts & FontOpts::Italic) return italic;
    else return font_;
  }
  bool is_monospace() const { return is_mono; }
private:
  void update()
  {
    QFontMetricsF metrics {font_};
    bolditalic = font_;
    bold = font_;
    italic = font_;
    bolditalic.setBold(true);
    bolditalic.setItalic(true);
    bold.setBold(true);
    italic.setItalic(true);
    is_mono = metrics.horizontalAdvance('W') == metrics.horizontalAdvance('a');
  }
  QFont font_;
  QFont bolditalic;
  QFont bold;
  QFont italic;
  QRawFont raw_;
  bool is_mono;
};

/// Font dimensions of a monospace font,
/// stores the width and height of a single character.
struct FontDimensions
{
  float width;
  float height;
};
#endif // NVUI_FONT_HPP
