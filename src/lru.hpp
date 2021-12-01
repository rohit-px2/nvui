#ifndef NVUI_LRU_HPP
#define NVUI_LRU_HPP

#include <cstdint>
#include <list>
#include <QHash>
#include <QPair>

// Default delete does nothing (variables clean up themselves)
template<typename T>
struct do_nothing_deleter
{
  void operator()(T* p) const { Q_UNUSED(p); }
};

/// LRUCache with optional custom deleter
/// I use the custom deleter for IDWriteTextLayout1
/// so they get automatically released
/// The caching strategy for QPaintGrid (using QStaticText)
/// cleans up itself so the deleter doesn't need to be specified
/// Once again, using Neovide's idea of caching text blobs.
/// See
/// https://github.com/neovide/neovide/blob/main/src/renderer/fonts/caching_shaper.rs
/// Uses QHash internally since it's expected to be used with QString
/// to cache drawn text.
template<typename K, typename V, typename ValueDeleter = do_nothing_deleter<V>>
class LRUCache
{
public:
  using key_type = K;
  using value_type = V;
  LRUCache(std::size_t capacity)
    : max_size(capacity),
      map(),
      keys()
  {
    map.reserve(static_cast<int>(max_size));
  }

  V& put(K k, V v)
  {
    V* ptr = nullptr;
    auto it = map.find(k);
    if (it != map.end())
    {
      it->first = std::move(v);
      ptr = &it->first;
      move_to_front(it->second);
    }
    else
    {
      keys.push_front(k);
      auto& pair_ref = map[k];
      pair_ref = {v, keys.begin()};
      ptr = &pair_ref.first;
    }
    if (keys.size() > max_size)
    {
      const auto& back_key = keys.back();
      auto erase_it = map.find(back_key);
      ValueDeleter()(std::addressof(erase_it->first));
      map.erase(erase_it);
      keys.pop_back();
    }
    return *ptr;
  }

  V* get(const K& k)
  {
    auto it = map.find(k);
    if (it != map.end())
    {
      move_to_front(it->second);
      return std::addressof(it->first);
    }
    else
    {
      return nullptr;
    }
  }

  ~LRUCache()
  {
    constexpr auto deleter = ValueDeleter();
    for(auto& val : map) deleter(std::addressof(val.first));
  }

  void clear()
  {
    constexpr auto deleter = ValueDeleter();
    for(auto& val : map) deleter(std::addressof(val.first));
    map.clear();
    keys.clear();
  }

private:
  // https://stackoverflow.com/questions/14579957/std-container-c-move-to-front
  void move_to_front(typename std::list<K>::iterator it)
  {
    if (it != keys.begin()) keys.splice(keys.begin(), keys, it);
  }
  std::size_t max_size = 10;
  QHash<K, QPair<V, typename std::list<K>::iterator>> map;
  std::list<K> keys;
};

#endif // NVUI_LRU_HPP
