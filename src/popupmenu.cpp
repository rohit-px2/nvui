#include "popupmenu.hpp"
#include <iostream>

void PopupMenu::pum_show(const msgpack::object* obj, std::uint32_t size)
{
  using std::tuple;
  using std::vector;
  using std::string;
  for(std::uint32_t i = 0; i < size; ++i)
  {
    const msgpack::object& o = obj[i];
    assert(o.type == msgpack::type::ARRAY);
    const msgpack::object_array& arr = o.via.array;
    assert(arr.size == 5);
    assert(arr.ptr[0].type == msgpack::type::ARRAY);
    const msgpack::object_array& items = arr.ptr[0].via.array;
    add_items(items);
    const int selected = arr.ptr[1].as<int>();
    const int row = arr.ptr[2].as<int>();
    const int col = arr.ptr[3].as<int>();
    const int grid = arr.ptr[4].as<int>();
  }
}

void PopupMenu::pum_sel(const msgpack::object* obj, std::uint32_t size)
{
}

void PopupMenu::pum_hide(const msgpack::object* obj, std::uint32_t size)
{
}

void PopupMenu::add_items(const msgpack::object_array& items)
{
}
