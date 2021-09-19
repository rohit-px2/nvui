#include "msgpack_overrides.hpp"
#include "object.hpp"
#include <iostream>
#include <span>
#include <stack>
#include <fmt/core.h>
#include <fmt/format.h>

Object Object::parse(const msgpack::object& obj)
{
  using namespace msgpack::type;
  switch(obj.type)
  {
    case NIL:
      return std::monostate {};
    case msgpack::type::BOOLEAN:
      return obj.via.boolean;
    case POSITIVE_INTEGER:
      return obj.via.u64;
    case NEGATIVE_INTEGER:
      return obj.via.i64;
    case STR:
      return obj.as<QString>();
    case BIN:
      return obj.as<std::string>();
    case EXT:
    {
      const auto& obj_ext = obj.via.ext;
      NeovimExt ext;
      ext.type = obj_ext.type();
      ext.data = std::string(obj_ext.data(), obj_ext.size);
      return ext;
    }
    case FLOAT32:
    case FLOAT64:
      return obj.as<double>();
    case ARRAY:
    {
      ObjectArray vo;
      vo.reserve(obj.via.array.size);
      std::span s {obj.via.array.ptr, obj.via.array.size};
      for(const auto& o : s) vo.emplace_back(parse(o));
      return vo;
    }
    case MAP:
    {
      ObjectMap mp;
      const auto& obj_map = obj.via.map;
      mp.reserve(obj_map.size);
      std::span<msgpack::object_kv> aop {obj_map.ptr, obj_map.size};
      for(const auto& kv : aop)
      {
        assert(kv.key.type == msgpack::type::STR);
        mp.emplace(kv.key.as<std::string>(), parse(kv.val));
      }
      return mp;
    }
  }
  return std::monostate {};
}

template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

void Object::to_stream(std::stringstream& ss) const
{
  std::visit(overloaded {
    [&](const std::monostate&) { ss << "null"; },
    [&](const std::string& s) { ss << '"' << s << '"'; },
    [&](const QString& q) { ss << '\"' << q.toStdString() << '\"'; },
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
    [&](const QByteArray& ba) {
      ss << '[';
      for(int i = 0; i < ba.size() - 1; ++i)
      {
        ss << static_cast<std::uint8_t>(ba.at(i));
      }
      if (!ba.isEmpty()) ss << static_cast<std::uint8_t>(ba.back());
      ss << ']';
    }
  }, *this);
}

std::string Object::to_string() const
{
  std::stringstream ss;
  ss << std::boolalpha;
  to_stream(ss);
  return ss.str();
}


struct MsgpackVisitor
{
  enum Error
  {
    None,
    InsufficentBytes,
    Parse
  };
  Error error = Error::None;
  bool map_key_ended = false;
  Object result {};
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
    return true;
  }

  bool end_map() { pop_stack(); return true; }
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
    place(NeovimExt {type, std::string(v + 1, size - 1)});
    return true;
  }


  bool visit_str(const char* v, std::uint32_t len)
  {
    if (current && current->has<ObjectMap>() && !map_key_ended)
    {
      place(std::string(v, len));
      return true;
    }
    place(QString::fromUtf8(v, static_cast<int>(len)));
    return true;
  }

  void parse_error(std::size_t, std::size_t)
  {
    // Log?
    error = Error::Parse;
  }

  void insufficient_bytes(std::size_t, std::size_t)
  {
    // Log?
    error = Error::InsufficentBytes;
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

Object Object::from_msgpack(std::string_view sv, std::size_t& offset)
{
  MsgpackVisitor v;
  msgpack::parse(sv.data(), sv.size(), offset, v);
  if (v.error != MsgpackVisitor::Error::None)
  {
    fmt::print(
      "Error occurred while parsing messagepack string: "
      "{}\n",
      v.error == MsgpackVisitor::Error::InsufficentBytes
        ? "Insufficient Bytes" : "Parse error"
    );
    return Object();
  }
  Object o = std::move(v.result);
  return o;
}
