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
#include <boost/container/flat_map.hpp>
#include <boost/container/small_vector.hpp>
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
  static const Object null;
  using Map = boost::container::flat_map<std::string, Object, std::less<>>;
  using Array = std::vector<Object>;
  using null_type = std::monostate;
  using string_type = std::string;
  using signed_type = std::int64_t;
  using unsigned_type = std::uint64_t;
  using array_type = Array;
  using map_type = Map;
  using bool_type = bool;
  using ext_type = NeovimExt;
  using float_type = double;
  using err_type = Error;
  using variant_type = std::variant<
    null_type,
    signed_type,
    unsigned_type,
    string_type,
    array_type,
    map_type,
    bool_type,
    ext_type,
    float_type,
    err_type
  >;
  template<typename T>
  Object(T&& t): v(std::forward<T>(t)) {}
  Object() = default;
  Object(Object&&) = default;
  Object(const Object&) = default;
  Object& operator=(const Object&) = default;
  Object& operator=(Object&&) = default;
  ~Object();
  /// Parses an Object from a msgpack string.
  /// Note: This doesn't support the full messagepack specification,
  /// since map keys are always strings, but Neovim always specifies
  /// keys as strings, so it works.
  static Object from_msgpack(std::string_view sv, std::size_t& offset) noexcept;
  /// Parse from a msgpack::object from the msgpack-cpp library.
  /// This is recursive and can get slow.
  /// For a faster solution parse the object directly
  /// from the serialized string using Object::from_msgpack.
  static Object parse(const msgpack::object&);
  std::string to_string() const noexcept;
  auto* array() noexcept { return std::get_if<Array>(&v); }
  auto* string() noexcept { return std::get_if<string_type>(&v); }
  auto* i64() noexcept { return std::get_if<int64_t>(&v); }
  auto* u64() noexcept { return std::get_if<uint64_t>(&v); }
  auto* map() noexcept { return std::get_if<Map>(&v); }
  auto* boolean() noexcept { return std::get_if<bool>(&v); }
  auto* f64() noexcept { return std::get_if<double>(&v); }
  auto* ext() noexcept { return std::get_if<NeovimExt>(&v); }
  auto* err() noexcept { return std::get_if<Error>(&v); }
  // Const versions
  auto* array() const noexcept { return std::get_if<Array>(&v); }
  auto* string() const noexcept { return std::get_if<string_type>(&v); }
  auto* i64() const noexcept { return std::get_if<int64_t>(&v); }
  auto* u64() const noexcept { return std::get_if<uint64_t>(&v); }
  auto* map() const noexcept { return std::get_if<Map>(&v); }
  auto* boolean() const noexcept { return std::get_if<bool>(&v); }
  auto* f64() const noexcept { return std::get_if<double>(&v); }
  auto* ext() const noexcept { return std::get_if<NeovimExt>(&v); }
  auto* err() const noexcept { return std::get_if<Error>(&v); }
  bool is_null() const noexcept { return has<null_type>(); }
  bool is_err() const noexcept { return has<err_type>(); }
  bool is_string() const noexcept { return has<string_type>(); }
  bool is_array() const noexcept { return has<array_type>(); }
  bool is_map() const noexcept { return has<map_type>(); }
  bool is_signed() const { return has<signed_type>(); }
  bool is_unsigned() const { return has<unsigned_type>(); }
  bool is_float() const { return has<float_type>(); }
  bool is_ext() const { return has<ext_type>(); }
  bool is_bool() const { return has<bool>(); }
  template<typename T>
  bool has() const noexcept
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
  bool convertible() const noexcept
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
  std::optional<T> try_convert() const noexcept
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
  std::optional<std::tuple<T...>> try_decompose() const noexcept
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

  const Object& try_at(std::string_view s) const noexcept
  {
    if (!has<Map>()) return null;
    const auto& mp = get<Map>();
    const auto it = mp.find(s);
    if (it == mp.cend()) return null;
    return it->second;
  }

  const Object& try_at(std::size_t idx) const noexcept
  {
    if (!has<Array>()) return null;
    const auto& arr = get<Array>();
    if (idx >= arr.size()) return null;
    return arr.at(idx);
  }

private:
  std::size_t children() const noexcept;
  void to_stream(std::stringstream& ss) const;
  variant_type v;
};

using ObjectArray = Object::Array;
using ObjectMap = Object::Map;

#endif // NVUI_OBJECT_HPP
