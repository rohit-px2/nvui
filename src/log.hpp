#ifndef NVUI_LOG_HPP
#define NVUI_LOG_HPP
#include <spdlog/spdlog.h>
namespace log
{

template<typename Func>
void log_if(spdlog::level::level_enum level, Func&& f)
{
  if (spdlog::should_log(level)) spdlog::log(level, std::invoke(f));
}

template<typename Func>
void lazy_warn(Func&& f)
{
  return log_if(spdlog::level::warn, f);
}

template<typename Func>
void lazy_trace(Func&& f)
{
   return log_if(spdlog::level::trace, f);
}

template<typename Func>
void lazy_err(Func&& f)
{
  return log_if(spdlog::level::err, f);
}

template<typename Func>
void lazy_info(Func&& f)
{
  return log_if(spdlog::level::info, f);
}

template<typename Func>
void lazy_critical(Func&& f)
{
  return log_if(spdlog::level::critical, f);
}

} // namespace log

#define LOG_LAZY(level, ...) \
  if (spdlog::should_log(level)) spdlog::log(level, __VA_ARGS__);

#define LOG_TRACE(...) LOG_LAZY(spdlog::level::trace, __VA_ARGS__)
#define LOG_WARN(...) LOG_LAZY(spdlog::level::warn, __VA_ARGS__)
#define LOG_INFO(...) LOG_LAZY(spdlog::level::info, __VA_ARGS__)
#define LOG_ERROR(...) LOG_LAZY(spdlog::level::err, __VA_ARGS__)
#define LOG_CRITICAL(...) LOG_LAZY(spdlog::level::critical, __VA_ARGS__)

#endif // NVUI_LOG_HPP
