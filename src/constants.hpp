#ifndef NVUI_CONSTANTS_HPP
#define NVUI_CONSTANTS_HPP

#include <QString>
#include "utils.hpp"

namespace constants
{
  inline const QString& picon_fp()
  {
    static const QString picon_fp = normalize_path("../assets/icons/popup/");
  	return picon_fp;
	}

  inline const QString& appicon()
	{
		static const QString appicon = normalize_path("../assets/appicon.png");
		return appicon;
	}

  inline const QString& maxicon()
	{
		static const QString maxicon = normalize_path("../assets/max-windows.svg");
		return maxicon;
	}

  inline const QString& maxicon_second()
	{
		static const QString maxicon_second = normalize_path("../assets/max-windows-second.svg");
		return maxicon_second;
	}

  inline const QString& minicon()
  {
		static const QString minicon = normalize_path("../assets/min-windows.svg");
		return minicon;
	}

  inline const QString& closeicon()
	{
		static const QString closeicon = normalize_path("../assets/close-windows.svg");
		return closeicon;
	}

} // namespace constants
#endif
