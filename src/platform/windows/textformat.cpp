#include "textformat.hpp"

using Microsoft::WRL::ComPtr;

DWRITE_FONT_STYLE dwrite_style(const FontOpts& fo)
{
  if (fo & FontOpts::Italic) return DWRITE_FONT_STYLE_ITALIC;
  return DWRITE_FONT_STYLE_NORMAL;
}

DWRITE_FONT_WEIGHT dwrite_weight(const FontOpts& fo)
{
  if (fo & FontOpts::Normal) return DWRITE_FONT_WEIGHT_NORMAL;
  if (fo & FontOpts::Thin) return DWRITE_FONT_WEIGHT_THIN;
  if (fo & FontOpts::Light) return DWRITE_FONT_WEIGHT_LIGHT;
  if (fo & FontOpts::Medium) return DWRITE_FONT_WEIGHT_MEDIUM;
  if (fo & FontOpts::SemiBold) return DWRITE_FONT_WEIGHT_SEMI_BOLD;
  if (fo & FontOpts::Bold) return DWRITE_FONT_WEIGHT_BOLD;
  if (fo & FontOpts::ExtraBold) return DWRITE_FONT_WEIGHT_EXTRA_BOLD;
  return DWRITE_FONT_WEIGHT_NORMAL;
}

DWRITE_HIT_TEST_METRICS metrics_for(
  std::wstring_view text,
  IDWriteFactory* factory,
  IDWriteTextFormat* text_format
)
{
  ComPtr<IDWriteTextLayout> tl = nullptr;
  factory->CreateTextLayout(
    text.data(),
    (UINT32) text.size(),
    text_format,
    std::numeric_limits<float>::max(),
    std::numeric_limits<float>::max(),
    &tl
  );
  DWRITE_HIT_TEST_METRICS ht_metrics;
  float ignore;
  tl->HitTestTextPosition(0, 0, &ignore, &ignore, &ht_metrics);
  return ht_metrics;
}

TextFormat::TextFormat(
  IDWriteFactory* factory,
  const std::wstring& name,
  float pointsize,
  float dpi,
  FontOpts default_weight,
  FontOpts default_style
)
{
  auto w = dwrite_weight(default_weight);
  auto s = dwrite_style(default_style);
  // Create reg with default weight
  const auto create = [&](auto pptf, auto weight, auto style) {
    factory->CreateTextFormat(
      name.c_str(),
      nullptr,
      weight,
      style,
      DWRITE_FONT_STRETCH_NORMAL,
      pointsize * (dpi / 72.0f),
      L"en-us",
      pptf
    );
  };
  create(reg.GetAddressOf(), w, s);
  create(bold.GetAddressOf(), DWRITE_FONT_WEIGHT_BOLD, s);
  create(italic.GetAddressOf(), w, DWRITE_FONT_STYLE_ITALIC);
  create(bolditalic.GetAddressOf(), DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_ITALIC);
  auto w_metrics = metrics_for(L"W", factory, reg.Get());
  auto a_metrics = metrics_for(L"a", factory, reg.Get());
  is_mono = w_metrics.width == a_metrics.width;
}

IDWriteTextFormat* TextFormat::font_for(const FontOptions& fo) const
{
  if (fo & FontOpts::Italic && fo & FontOpts::Bold)
  {
    return bolditalic.Get();
  }
  else if (fo & FontOpts::Bold) return bold.Get();
  else if (fo & FontOpts::Italic) return italic.Get();
  else return reg.Get();
}

bool TextFormat::is_monospace() const
{
  return is_mono;
}
