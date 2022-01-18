#ifndef NVUI_FONTDESC_HPP
#define NVUI_FONTDESC_HPP

#include <string>
#include "hlstate.hpp"

struct FontDesc
{
  std::string name;
  double point_size;
  FontOptions base_options;
};

#endif // NVUI_FONTDESC_HPP
