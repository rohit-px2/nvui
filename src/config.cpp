#include "config.hpp"
#include "utils.hpp"

std::unique_ptr<QSettings> Config::settings_ptr = nullptr;

void Config::init()
{
  const auto ini_path = normalize_path("nvui-config.ini");
  if (!settings_ptr)
  {
    settings_ptr = std::make_unique<QSettings>(ini_path, QSettings::IniFormat);
  }
}

QVariant Config::get(const QString& key, const QVariant& default_val)
{
  return settings_ptr->value(key, default_val);
}

void Config::set(const QString& key, const QVariant& value)
{
  settings_ptr->setValue(key, value);
}

bool Config::is_set(const QString& key)
{
  return settings_ptr->contains(key);
}

void Config::remove_key(const QString& key)
{
  settings_ptr->remove(key);
}

void Config::clear()
{
  settings_ptr->clear();
}
