#include "grid.hpp"
#include "scalers.hpp"

scalers::time_scaler GridBase::scroll_scaler = scalers::oneminusexpo2negative10;
scalers::time_scaler GridBase::move_scaler = scalers::oneminusexpo2negative10;

grid_char GridChar::grid_char_from_str(const std::string& s)
{
  return QString::fromStdString(s);
}

