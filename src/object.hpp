#ifndef NVUI_OBJECT_HPP
#define NVUI_OBJECT_HPP

#include <msgpack.hpp>
#include <span>
#include <string>
#include <cstdint>
#include <optional>
#include <variant>
#include <tuple>
#include <QByteArray>
#include <QString>
#include <string_view>
#include <boost/container/vector.hpp>
#include <boost/container/flat_map.hpp>
#include "utils.hpp"

struct NeovimExt
{
  std::int8_t type;
  QByteArray data;
};

struct Error
{
  std::string_view msg; // Constant message
};

struct Object
{
  using Map = boost::container::flat_map<std::string, Object, std::less<>>;
  using Array = std::vector<Object>;
  using variant_type = std::variant<
    std::monostate,
    int64_t,
    uint64_t,
    QString,
    Array,
    Map,
    bool,
    NeovimExt,
    std::string,
    double,
    Error
  >;
  template<typename T>
  Object(T&& t): v(std::forward<T>(t)) {}
  Object() = default;
  Object(Object&&) = default;
  Object(const Object&) = default;
  Object& operator=(const Object&) = default;
  Object& operator=(Object&&) = default;
  /// Parses an Object from a msgpack string.
  /// Note: This doesn't support the full messagepack specification,
  /// since map keys are always strings, but Neovim always specifies
  /// keys as strings, so it works.
  static Object from_msgpack(std::string_view sv, std::size_t& offset);
  /// Parse from a msgpack::object from the msgpack-cpp library.
  /// This is recursive and can get slow.
  /// For a faster solution parse the object directly
  /// from the serialized string using Object::from_msgpack.
  static Object parse(const msgpack::object&);
  std::string to_string() const;
  auto* array() { return std::get_if<Array>(&v); }
  auto* string() { return std::get_if<QString>(&v); }
  auto* i64() { return std::get_if<int64_t>(&v); }
  auto* u64() { return std::get_if<uint64_t>(&v); }
  auto* map() { return std::get_if<Map>(&v); }
  auto* boolean() { return std::get_if<bool>(&v); }
  auto* f64() { return std::get_if<double>(&v); }
  auto* ext() { return std::get_if<NeovimExt>(&v); }
  auto* err() { return std::get_if<Error>(&v); }
  // Const versions
  auto* array() const { return std::get_if<Array>(&v); }
  auto* string() const { return std::get_if<QString>(&v); }
  auto* i64() const { return std::get_if<int64_t>(&v); }
  auto* u64() const { return std::get_if<uint64_t>(&v); }
  auto* map() const { return std::get_if<Map>(&v); }
  auto* boolean() const { return std::get_if<bool>(&v); }
  auto* f64() const { return std::get_if<double>(&v); }
  auto* ext() const { return std::get_if<NeovimExt>(&v); }
  auto* err() const { return std::get_if<Error>(&v); }
  bool is_null() const { return has<std::monostate>(); }
  bool has_err() const { return has<Error>(); }
  template<typename T>
  bool has() const
  {
    return std::holds_alternative<T>(v);
  }
  template<typename T>
  explicit operator T() const
  {
    return std::visit([](const auto& val) -> T {
      if constexpr(std::is_convertible_v<decltype(val), T>)
      {
        return T(val);
      }
      else
      {
        throw std::bad_variant_access();
      }
    }, v);
  }

  template<typename T>
  bool convertible() const
  {
    return std::visit([](const auto& val) -> bool {
      return std::is_convertible_v<decltype(val), T>;
    }, v);
  }

  template<typename T>
  T& get()
  {
    return std::get<T>(v);
  }

  template<typename T>
  const T& get() const
  {
    return std::get<T>(v);
  }

  template<typename T>
  std::optional<T> try_convert() const
  {
    using type = std::optional<T>;
    return std::visit([](const auto& arg) -> type {
      if constexpr(std::is_convertible_v<decltype(arg), T>)
      {
        return std::optional<T>(arg);
      }
      return std::nullopt;
    }, v);
  }

  /// Decompose an Object to multiple values.
  /// The Object you call this on should be an array,
  /// otherwise std::nullopt will always be returned.
  /// If type conversion fails for any value, std::nullopt
  /// is returned.
  template<typename... T>
  std::optional<std::tuple<T...>> try_decompose() const
  {
    if (!has<Array>()) return std::nullopt;
    //using opt_tuple_type = std::optional<std::tuple<T...>>;
    const auto& arr = get<Array>();
    std::tuple<T...> t;
    std::size_t idx = 0;
    bool valid = true;
    if (sizeof...(T) > arr.size()) return {};
    for_each_in_tuple(t, [&](auto& elem) {
      using elem_type = std::remove_reference_t<decltype(elem)>;
      auto v = arr.at(idx).try_convert<elem_type>();
      if (!v) { valid = false; }
      else elem = *v;
      ++idx;
    });
    if (valid) return t;
    return {};
  }

  std::optional<std::reference_wrapper<const Object>>
  try_at(std::string_view s) const
  {
    if (!has<Map>()) return {};
    const auto& mp = get<Map>();
    const auto it = mp.find(s);
    if (it == mp.cend()) return {};
    return it->second;
  }

  std::optional<std::reference_wrapper<const Object>>
  try_at(std::size_t idx) const
  {
    if (!has<Array>()) return {};
    const auto& arr = get<Array>();
    if (idx >= arr.size()) return {};
    return arr.at(idx);
  }

private:
  void to_stream(std::stringstream& ss) const;
  variant_type v;
};

using ObjectArray = Object::Array;
using ObjectMap = Object::Map;

#endif // NVUI_OBJECT_HPP
