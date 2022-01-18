#ifndef NVUI_NVIM_UTILS_HPP
#define NVUI_NVIM_UTILS_HPP

#include <functional>
#include <QString>
#include "object.hpp"
#include "nvim.hpp"

/**
 * Automatically unpack ObjectArray into the desired parameters,
 * or exit if it doesn't match.
 */
template<typename... T, typename Func>
std::function<void (const ObjectArray&)> paramify(Func&& f)
{
  return [f](const ObjectArray& arg_list) {
    std::tuple<T...> t;
    constexpr std::size_t types_len = sizeof...(T);
    if (arg_list.size() < types_len) return;
    bool valid = true;
    std::size_t idx = 0;
    for_each_in_tuple(t, [&](auto& p) {
      using val_type = std::remove_reference_t<decltype(p)>;
      if constexpr(std::is_same_v<val_type, QString>)
      {
        std::optional<QString> s {};
        if (auto* o_s = arg_list.at(idx).string())
        {
          s = QString::fromStdString(*o_s);
        }
        if (!s) { valid = false; ++idx; return; }
        else p = std::move(s.value());
        ++idx;
        return;
      }
      std::optional<val_type> v = arg_list.at(idx).try_convert<val_type>();
      if (!v) { valid = false; return; }
      p = std::move(v.value());
      ++idx;
    });
    if (!valid) return;
    std::apply(f, t);
  };
}

// Run a function when the given Nvim object receieves a notification
// with name 'method_name'.
inline void listen_for_notification(
  Nvim& nvim,
  std::string method,
  std::function<void (const ObjectArray&)> func,
  QObject* target
)
{
  nvim.set_notification_handler(
    std::move(method),
    [target, cb = std::move(func)](Object obj) {
      QMetaObject::invokeMethod(
        target,
        [o = std::move(obj), f = std::move(cb)] {
          auto* arr = o.array();
          if (!(arr && arr->size() >= 3)) return;
          auto* params = arr->at(2).array();
          if (!params) return;
          f(std::move(*params));
        },
        Qt::QueuedConnection
      );
    }
  );
}

template<typename Res, typename Err>
void handle_request(
  Nvim& nvim,
  const std::string& method,
  std::function<
    std::tuple<std::optional<Res>, std::optional<Err>> (const ObjectArray&)
  > func,
  QObject* target
)
{
  nvim.set_request_handler(
    method,
    [target, cb = std::move(func), &nvim](Object obj) {
      QMetaObject::invokeMethod(
        target,
        [o = std::move(obj), f = std::move(cb), &nvim] {
          auto* arr = o.array();
          if (!arr || arr->size() < 4) return;
          auto msgid = arr->at(1).u64();
          auto params = arr->at(3).array();
          if (!msgid || !params) return;
          std::optional<Res> res;
          std::optional<Err> err;
          std::tie(res, err) = f(*arr);
          if (res)
          {
            nvim.send_response(*msgid, *res, msgpack::object());
          }
          else
          {
            nvim.send_response(*msgid, msgpack::object(), *err);
          }
        }
      );
  });
}

#endif // NVUI_NVIM_UTILS_HPP
