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
  std::string data; // Preserve byte layout
};

struct Error
{
  std::string_view msg; // Constant message
};

struct Object;
/// Neovim's Dictionary type only uses strings as keys.
using ObjectMap = boost::container::flat_map<std::string, Object>;
using ObjectArray = boost::container::vector<Object>;
using variant_type = std::variant<
  std::monostate,
  int64_t,
  uint64_t,
  QString,
  ObjectArray,
  ObjectMap,
  bool,
  NeovimExt,
  std::string,
  double,
  Error
>;

struct Object : public variant_type
{
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
  auto* array() { return get_if<ObjectArray>(this); }
  auto* string() { return get_if<QString>(this); }
  auto* i64() { return get_if<int64_t>(this); }
  auto* u64() { return get_if<uint64_t>(this); }
  auto* map() { return get_if<ObjectMap>(this); }
  auto* boolean() { return get_if<bool>(this); }
  auto* f64() { return get_if<double>(this); }
  auto* ext() { return get_if<NeovimExt>(this); }
  auto* err() { return get_if<Error>(this); }
  // Const versions
  auto* array() const { return get_if<ObjectArray>(this); }
  auto* string() const { return get_if<QString>(this); }
  auto* i64() const { return get_if<int64_t>(this); }
  auto* u64() const { return get_if<uint64_t>(this); }
  auto* map() const { return get_if<ObjectMap>(this); }
  auto* boolean() const { return get_if<bool>(this); }
  auto* f64() const { return get_if<double>(this); }
  auto* ext() const { return get_if<NeovimExt>(this); }
  auto* err() const { return get_if<Error>(this); }
  bool is_null() const { return has<std::monostate>(); }
  bool has_err() const { return has<Error>(); }
  template<typename T>
  bool has() const
  {
    return std::holds_alternative<T>(*this);
  }
  template<typename T>
  operator T() const
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
    }, *this);
  }

  template<typename T>
  bool convertible() const
  {
    return std::visit([](const auto& val) -> bool {
      return std::is_convertible_v<decltype(val), T>;
    }, *this);
  }

  template<typename T>
  bool is_convertible() const
  {
    return std::visit([](const auto& v) {
      return std::is_convertible_v<decltype(v), T>;
    }, *this);
  }

  template<typename T>
  T& get()
  {
    return std::get<T>(*this);
  }

  template<typename T>
  const T& get() const
  {
    return std::get<T>(*this);
  }

  template<typename T>
  std::optional<T> get_as() const
  {
    using type = std::optional<T>;
    return std::visit([](const auto& arg) -> type {
      if constexpr(std::is_convertible_v<decltype(arg), T>)
      {
        return std::optional<T>(arg);
      }
      return std::nullopt;
    }, *this);
  }

  /// Decompose an Object to multiple values.
  /// The Object you call this on must be an ObjectArray.
  /// If type conversion fails for any value, std::nullopt
  /// is returned.
  template<typename... T>
  std::optional<std::tuple<T...>> decompose() const
  {
    assert(has<ObjectArray>());
    //using opt_tuple_type = std::optional<std::tuple<T...>>;
    const auto& arr = get<ObjectArray>();
    std::tuple<T...> t;
    std::size_t idx = 0;
    bool valid = true;
    if (sizeof...(T) > arr.size()) return {};
    for_each_in_tuple(t, [&](auto& elem) {
      using elem_type = std::remove_reference_t<decltype(elem)>;
      auto v = arr.at(idx).get_as<elem_type>();
      if (!v) { valid = false; }
      else elem = *v;
      ++idx;
    });
    if (valid) return t;
    return {};
  }

public:
  using variant_type::variant_type;
  using variant_type::operator=;
private:
  void to_stream(std::stringstream& ss) const;
};

#endif // NVUI_OBJECT_HPP
