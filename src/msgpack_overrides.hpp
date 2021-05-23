#ifndef NVUI_MSGPACK_OVERRIDES_HPP
#define NVUI_MSGPACK_OVERRIDES_HPP

// Make msgpack work for QString mostly,
// but others can be added as well
// This lets us avoid having to create an std::string in the middle
#include <cassert>
#include <msgpack.hpp>
#include <QString>

namespace msgpack
{
  MSGPACK_API_VERSION_NAMESPACE(MSGPACK_DEFAULT_API_NS)
  {
    namespace adaptor
    {
      template<>
      struct as<QString>
      {
        QString operator()(const msgpack::object& o) const
        {
          assert(o.type == msgpack::type::STR);
          return QString::fromLocal8Bit(o.via.str.ptr, o.via.str.size);
        }
      };
    } // adaptor
  }
} // msgpack

#endif // NVUI_MSGPACK_OVERRIDES_HPP
