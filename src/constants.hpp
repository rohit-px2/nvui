#ifndef NVUI_CONSTANTS_HPP
#define NVUI_CONSTANTS_HPP

#include <QDir>
#include <QString>
#include <QProcess>
#include "utils.hpp"

namespace constants
{
	inline QString first_valid_folder(std::vector<QString> paths)
	{
		for(const auto& path : paths)
		{
			if (QDir(path).exists()) return path;
		}
		return paths.front();
	}
	inline QString env_val_or(QString env_name, QString fallback)
	{
		return QProcessEnvironment::systemEnvironment().value(env_name, fallback);
	}
	inline const QString& asset_dir()
	{
		static const QString asset_path = env_val_or("NVUI_ASSET_DIR", first_valid_folder({
			normalize_path("./assets"),
			normalize_path("../assets")
		}));
		return asset_path;
	}
  inline const QString& picon_fp()
  {
    static const QString picon_fp = asset_dir() + "/icons/popup/";
  	return picon_fp;
	}

  inline const QString& appicon()
	{
		static const QString appicon = asset_dir() + "/appicon.png";
		return appicon;
	}

  inline const QString& maxicon()
	{
		static const QString maxicon = asset_dir() + "/max-windows.svg";
		return maxicon;
	}

  inline const QString& maxicon_second()
	{
		static const QString maxicon_second = asset_dir() + "/max-windows-second.svg";
		return maxicon_second;
	}

  inline const QString& minicon()
  {
		static const QString minicon = asset_dir() + "/min-windows.svg";
		return minicon;
	}

  inline const QString& closeicon()
	{
		static const QString closeicon = asset_dir() + "/close-windows.svg";
		return closeicon;
	}
	
	/// Directory for all the Vim files (plugin, doc)
	inline const QString& script_dir()
	{
		static const QString dir = env_val_or("NVUI_SCRIPT_DIR", first_valid_folder({
			normalize_path("./vim"),
			normalize_path("../vim")
		}));
		return dir;
	}

} // namespace constants
#endif
