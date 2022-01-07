#include "msgpack_overrides.hpp"
#include "object.hpp"
#include <iostream>
#include <span>
#include <sstream>
#include <stack>
#include <fmt/core.h>
#include <fmt/format.h>

const Object Object::null = std::monostate {};

template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

void Object::to_stream(std::stringstream& ss) const
{
  std::visit(overloaded {
    [&](const std::monostate&) { ss << "null"; },
    [&](const std::string& s) { ss << '"' << s << '"'; },
    [&](const int64_t& i) { ss << i; },
    [&](const uint64_t& u) { ss << u; },
    [&](const ObjectArray& v) {
      ss << '[';
      for(std::size_t i = 0; i < v.size() - 1 && i < v.size(); ++i)
      {
        v[i].to_stream(ss);
        ss << ", ";
      }
      if (!v.empty()) v.back().to_stream(ss);
      ss << ']';
    },
    [&](const ObjectMap& mp) {
      ss << '{';
      for (const auto& [k, v] : mp)
      {
        ss << '"' << k << "\":";
        v.to_stream(ss);
        ss << ", ";
      }
      ss << '}';
    },
    [&](const NeovimExt& ext) {
      Q_UNUSED(ext);
      ss << "EXT";
    },
    [&](const bool& b) { ss << b; },
    [&](const double& d) { ss << d; },
    [&](const Error& err) {
      ss << "Error: " << err.msg << '\n';
    }
  }, v);
}

std::string Object::to_string() const noexcept
{
  std::stringstream ss;
  ss << std::boolalpha;
  to_stream(ss);
  return ss.str();
}

/// Visitor that satisfies msgpack's visitor requirements
/// that parses messagepack data to an Object.
/// The final object is stored in "result".
/// To check if an error occurred during parsing,
/// check the 'error' variable. If no error occurred,
/// the error has value MsgpackVisitor::Error::None.
struct MsgpackVisitor
{
  enum Error
  {
    None,
    InsufficientBytesError,
    ParseError
  };
  MsgpackVisitor(Object& o): result(o), stack() {}
  Object& result;
  Error error = Error::None;
  bool in_map = false;
  bool map_key_ended = false;
  Object* current = nullptr;
  std::stack<Object*, std::vector<Object*>> stack;
  const std::string* cur_key = nullptr;

  bool start_array(std::uint32_t len)
  {
    stack.push(current);
    auto* obj = place(ObjectArray());
    assert(obj);
    obj->get<ObjectArray>().reserve(len);
    current = obj;
    return true;
  }

  bool start_map(std::uint32_t len)
  {
    stack.push(current);
    auto* obj = place(ObjectMap());
    assert(obj);
    obj->get<ObjectMap>().reserve(len);
    current = obj;
    map_key_ended = false; // Key goes in 1st
    in_map = true;
    return true;
  }

  bool end_map() { pop_stack(); in_map = false; return true; }
  bool end_array() { pop_stack(); return true; }
  bool start_array_item() { return true; }
  bool end_array_item() { return true; }
  bool start_map_key() { map_key_ended = false; return true; }
  bool end_map_key() { map_key_ended = true; return true; }
  bool start_map_value() { return true; }
  bool end_map_value() { return true; }

  bool visit_nil()
  {
    place(std::monostate {});
    return true;
  }

  bool visit_boolean(bool v)
  {
    place(v);
    return true;
  }

  bool visit_positive_integer(std::uint64_t v)
  {
    place(v);
    return true;
  }

  bool visit_negative_integer(std::int64_t v)
  {
    place(v);
    return true;
  }

  bool visit_float32(float v)
  {
    place(double(v));
    return true;
  }

  bool visit_float64(double v)
  {
    place(v);
    return true;
  }

  bool visit_bin(const char* v, std::uint32_t size)
  {
    place(std::string(v, size));
    return true;
  }

  bool visit_ext(const char* v, std::uint32_t size)
  {
    std::int8_t type = static_cast<int8_t>(*v);
    place(NeovimExt {type, QByteArray(v + 1, size - 1)});
    return true;
  }

  bool visit_str(const char* v, std::uint32_t len)
  {
    place(std::string(v, len));
    return true;
  }

  void parse_error(std::size_t, std::size_t)
  {
    // Log?
    error = ParseError;
  }

  void insufficient_bytes(std::size_t, std::size_t)
  {
    // Log?
    error = InsufficientBytesError;
  }

private:

  void pop_stack()
  {
    if (stack.empty()) return;
    current = stack.top();
    stack.pop();
  }

  /// Place the arg where it should go and return a pointer to
  /// it (most of the time).
  template<typename T>
  Object* place(T&& arg)
  {
    if (!current)
    {
      result = std::forward<T>(arg);
      return &result;
    }
    else if (current->has<ObjectArray>())
    {
      return &current->get<ObjectArray>().emplace_back(std::forward<T>(arg));
    }
    else if (current->has<ObjectMap>())
    {
      auto& mp = current->get<ObjectMap>();
      if (!cur_key)
      {
        if constexpr (std::is_same_v<T, std::string>)
        {
          auto p = mp.emplace(std::forward<T>(arg), Object());
          cur_key = &p.first->first;
          return nullptr;
        }
      }
      else
      {
        assert(mp.contains(*cur_key));
        Object* o = &mp[*cur_key];
        *o = std::forward<T>(arg);
        cur_key = nullptr;
        return o;
      }
    }
    return nullptr;
  }
};

Object Object::from_msgpack(std::string_view sv, std::size_t& offset) noexcept
{
  Object obj;
  MsgpackVisitor v {obj};
  msgpack::parse(sv.data(), sv.size(), offset, v);
  switch(v.error)
  {
    case MsgpackVisitor::None:
      return obj;
    case MsgpackVisitor::InsufficientBytesError:
      return Error {"Insufficient Bytes"};
    case MsgpackVisitor::ParseError:
      return Error {"Parse error"};
    default:
      return Error {""};
  }
}

Object Object::parse(const msgpack::object& obj)
{
  Object o;
  MsgpackVisitor v {o};
  msgpack::object_parser(obj).parse(v);
  return o;
}

std::size_t Object::children() const noexcept
{
  if (auto* arr = array()) { return arr->size(); }
  else if (auto* mp = map()) { return mp->size(); }
  else return 0;
}

Object::~Object()
{
  std::stack<Object*, std::vector<Object*>> stack;
  Object* cur = this;
  while(children() > 0)
  {
    if (cur->children() == 0)
    {
      cur = stack.top();
      stack.pop();
    }
    if (auto* arr = cur->array())
    {
      auto* obj = &arr->back();
      if (obj->children() > 0)
      {
        stack.push(cur);
        cur = obj;
      }
      else arr->pop_back();
    }
    else if (auto* map = cur->map())
    {
      // Keys are strings, no need to worry
      // Meanwhile values need to be checked
      auto it = map->end() - 1;
      auto* obj = &it->second;
      if (obj->children() > 0)
      {
        stack.push(cur);
        cur = obj;
      }
      else map->erase(map->end() - 1);
    }
  }
}
