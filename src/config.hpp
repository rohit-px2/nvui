#ifndef NVUI_SETTINGS_HPP
#define NVUI_SETTINGS_HPP

#include <QSettings>
#include <memory>

// External settings that aren't really runtime-configurable.
// This means things like default UI settings (multigrid being the biggest
// one, since it is not changeable at runtime), as well as startup
// position and size.
class Config
{
public:
  // Initialize config (load config file)
  // This must be done AFTER the QApplication is created
  // For a config file to be loaded it must be in the executable directory
  // under the name "nvui-config.ini".
  static void init();
  // init() MUST be called before the following functions
  static QVariant get(const QString& key, const QVariant& def_val = QVariant());
  static void set(const QString& key, const QVariant& value);
  static bool is_set(const QString& key);
  static void remove_key(const QString& key);
  static void clear();
private:
  static std::unique_ptr<QSettings> settings_ptr;
};

#endif // NVUI_SETTINGS_HPP
