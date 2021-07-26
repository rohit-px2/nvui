#ifndef NVUI_FONT_HPP
#define NVUI_FONT_HPP
#include <QFont>
#include <QFontMetrics>
#include <QRawFont>
class Font
{
public:
  const QFont& font() const { return font_; }
  const QRawFont& raw() const { return raw_; }
  Font(const QString& family): font_(family), raw_(QRawFont::fromFont(font_)) {}
  Font(const QFont& font): font_(font), raw_(QRawFont::fromFont(font_)) {}
  Font& operator=(const QFont& f)
  {
    font_ = f;
    raw_ = QRawFont::fromFont(font_);
    return *this;
  }
  Font& operator=(const QString& family)
  {
    font_.setFamily(family);
    raw_ = QRawFont::fromFont(font_);
    return *this;
  }
private:
  QFont font_;
  QRawFont raw_;
};

#endif // NVUI_FONT_HPP
