#ifndef NVUI_PLATFORM_WINDOWS_TEXTFORMAT_HPP
#define NVUI_PLATFORM_WINDOWS_TEXTFORMAT_HPP

#include <string>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include "hlstate.hpp"

struct TextFormat
{
  TextFormat(
    IDWriteFactory* factory,
    const std::wstring& name,
    float pointsize,
    float dpi,
    FontOpts default_weight,
    FontOpts default_style
  );
  IDWriteTextFormat* font_for(const FontOptions& fo) const;
  bool is_monospace() const;
  Microsoft::WRL::ComPtr<IDWriteTextFormat> reg = nullptr;
  Microsoft::WRL::ComPtr<IDWriteTextFormat> bold = nullptr;
  Microsoft::WRL::ComPtr<IDWriteTextFormat> bolditalic = nullptr;
  Microsoft::WRL::ComPtr<IDWriteTextFormat> italic = nullptr;
private:
  bool is_mono = true;
};

DWRITE_FONT_STYLE dwrite_style(const FontOpts& fo);
DWRITE_FONT_WEIGHT dwrite_weight(const FontOpts& fo);
DWRITE_HIT_TEST_METRICS metrics_for(
  std::wstring_view text,
  IDWriteFactory* factory,
  IDWriteTextFormat* text_format
);

#endif // NVUI_PLATFORM_WINDOWS_TEXTFORMAT_HPP
