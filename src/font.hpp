#ifndef NVUI_FONT_HPP
#define NVUI_FONT_HPP
#include <QFont>
#include <QFontMetrics>
#include <QRawFont>
#include "hlstate.hpp"

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
private:
  void update()
  {
    bolditalic = font_;
    bold = font_;
    italic = font_;
    bolditalic.setBold(true);
    bolditalic.setItalic(true);
    bold.setBold(true);
    italic.setItalic(true);
  }
  QFont font_;
  QFont bolditalic;
  QFont bold;
  QFont italic;
  QRawFont raw_;
};

/// Font dimensions of a monospace font,
/// stores the width and height of a single character.
struct FontDimensions
{
  float width;
  float height;
};
#endif // NVUI_FONT_HPP
