#ifndef NVUI_SCALERS_HPP
#define NVUI_SCALERS_HPP

#include <cmath>
#include <unordered_map>

namespace scalers
{
  // Follows the same idea
  // as Neovide's "ease" functions. time is between 0 - 1,
  // we can use some exponents to change the delta time so that
  // transition speed is not the same throughout.
  using time_scaler = float (*)(float);
  inline float oneminusexpo2negative10(float t)
  {
    // Taken from Neovide's "animation_utils.rs",
    // (specifically the "ease_out_expo" function).
    return 1.0f - std::pow(2.0, -10.0f * t);
  }
  inline float cube(float t)
  {
    return t * t * t;
  }
  inline float accel_continuous(float t)
  {
    return t * t * t * t;
  }
  inline float fast_start(float t)
  {
    return std::pow(t, 1.0/9.0);
  }
  inline float quadratic(float t)
  {
    return t * t;
  }
  inline float identity(float t)
  {
    return t;
  }
  /// Update this when a new scaler is added.
  inline const std::unordered_map<std::string, time_scaler>&
  scalers()
  {
    static const std::unordered_map<std::string, time_scaler> scaler_map {
      {"expo", oneminusexpo2negative10},
      {"cube", cube},
      {"fourth", accel_continuous},
      {"fast_start", fast_start},
      {"quad", quadratic},
      {"identity", identity}
    };
    return scaler_map;
  }
}

#endif // NVUI_SCALERS_HPP
