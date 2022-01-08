#ifndef NVUI_CONSTANTS_HPP
#define NVUI_CONSTANTS_HPP

#include <QString>
#include "utils.hpp"

namespace constants
{
  inline const QString& picon_fp()
  {
    static const QString picon_fp = ":/assets/icons/popup/";
  	return picon_fp;
	}

  inline const QString& appicon()
	{
		static const QString icon = ":/assets/appicon.png";
		return icon;
	}

  inline const QString& maxicon()
	{
		static const QString maxicon = ":/assets/max-windows.svg";
		return maxicon;
	}

  inline const QString& maxicon_second()
	{
		static const QString maxicon_second = ":/assets/max-windows-second.svg";
		return maxicon_second;
	}

  inline const QString& minicon()
  {
		static const QString minicon = ":/assets/min-windows.svg";
		return minicon;
	}

  inline const QString& closeicon()
	{
		static const QString closeicon = ":/assets/close-windows.svg";
		return closeicon;
	}
	
	/// Directory for runtime files (doc)
	inline const QString& script_dir()
	{
		static const QString dir = normalize_path("../vim");
		return dir;
	}

} // namespace constants
#endif
