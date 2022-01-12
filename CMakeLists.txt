cmake_minimum_required(VERSION 3.19)
#if(DEFINED ENV{QT_DIR})
  #message("Qt Dir is Set")
  #list(APPEND CMAKE_PREFIX_PATH "$ENV{QT_DIR}")
  #list(APPEND CMAKE_PREFIX_PATH "$ENV{QT_DIR}/Qt5Core")
  #list(APPEND CMAKE_PREFIX_PATH "$ENV{QT_DIR}/Qt5Widgets")
  #list(APPEND CMAKE_PREFIX_PATH "$ENV{QT_DIR}/Qt5Gui")
#else()
  #message("Could not find QT_DIR")
#endif()
#if (DEFINED ENV{QT_SVGDIR})
  #message("Qt SVG Dir is set")
  #list(APPEND CMAKE_PREFIX_PATH "$ENV{QT_SVGDIR}")
  #list(APPEND CMAKE_PREFIX_PATH "$ENV{QT_SVGDIR}/Qt5Svg")
  #list(APPEND CMAKE_PREFIX_PATH "$ENV{QT_SVGDIR}/Qt5Gui")
#endif()
option(BUILD_TESTS "Build tests" OFF)
if (BUILD_TESTS)
  list(APPEND VCPKG_MANIFEST_FEATURES "tests")
endif()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG")
endif()
project(nvui)
if(MSVC)
  #C5054 - Qt
  #C4702 - Msgpack
  #C4127 - Qt
  #C4458 - fmt
  add_compile_options(/W4 /WX /wd5054 /wd4459 /wd4458 /wd4244 /wd4127 /wd4702)
  set(CMAKE_CXX_FLAGS_RELEASE /O2 /DNDEBUG)
else()
  add_compile_options(-Wall -Wextra -pedantic -Werror -Wfatal-errors -Wno-language-extension-token -Wno-unknown-pragmas)
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
find_package(msgpack CONFIG REQUIRED)
find_package(Qt5 5.15.2 REQUIRED COMPONENTS Core Gui Svg Widgets)
find_package(fmt CONFIG REQUIRED)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
file(GLOB SOURCES
  src/titlebar.hpp
  src/titlebar.cpp
  src/utils.cpp
  src/nvim.hpp
  src/nvim.cpp
  src/hlstate.hpp
  src/hlstate.cpp
  src/window.hpp
  src/window.cpp
  src/cursor.hpp
  src/cursor.cpp
  src/popupmenu.hpp
  src/popupmenu.cpp
  src/cmdline.hpp
  src/cmdline.cpp
  src/font.hpp
  src/grid.cpp
  src/grid.hpp
  src/object.hpp
  src/object.cpp
  src/decide_renderer.hpp
  src/input.cpp
  src/input.hpp
  src/scalers.hpp
  src/lru.hpp
  src/qpaintgrid.hpp
  src/qpaintgrid.cpp
  src/animation.hpp
  src/animation.cpp
  src/types.hpp
  src/fontdesc.hpp
  src/mouse.hpp
  src/editor_base.cpp
  src/editor_base.hpp
  src/qt_editorui_base.cpp
  src/qt_editorui_base.hpp
  src/nvim_utils.hpp
  src/qeditor.hpp
  src/qeditor.cpp
  resources.qrc
)
if (WIN32)
  message("Windows detected.")
  file(GLOB WINONLYSOURCES
    src/platform/windows/*.cpp
    src/platform/windows/*.hpp
  )
  list(APPEND SOURCES ${WINONLYSOURCES})
endif()
find_package(Boost COMPONENTS filesystem thread REQUIRED)
list(APPEND SOURCES "src/main.cpp")
if(WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Release")
  add_executable(nvui WIN32 "assets/icons/desktop/neovim_icon.rc" ${SOURCES})
else()
  add_executable(nvui ${SOURCES})
endif()
target_link_libraries(nvui PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Svg)
target_link_libraries(nvui PRIVATE fmt::fmt)
target_include_directories(nvui PRIVATE
  "${PROJECT_SOURCE_DIR}/src"
)
if(WIN32)
  target_link_libraries(nvui PUBLIC
		d2d1
		dwrite
		dwmapi
		d3d11
  )
endif()
if (Boost_FOUND)
  target_include_directories(nvui PRIVATE ${Boost_INCLUDE_DIR})
endif()
target_link_libraries(nvui PRIVATE
  ${Boost_LIBRARIES}
)
include(CheckIPOSupported)
check_ipo_supported(RESULT LTOAvailable)
if(LTOAvailable)
  set_property(TARGET nvui PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

if(BUILD_TESTS)
  find_package(Catch2 CONFIG REQUIRED)
  file(GLOB TEST_SOURCES
    src/titlebar.hpp
    src/titlebar.cpp
    src/utils.hpp
    src/nvim.hpp
    src/nvim.cpp
    src/window.hpp
    src/window.cpp
    src/hlstate.hpp
    src/hlstate.cpp
    src/cursor.hpp
    src/cursor.cpp
    test/*.cpp
    src/popupmenu.hpp
    src/popupmenu.cpp
    src/cmdline.hpp
    src/cmdline.cpp
    src/font.hpp
    src/grid.cpp
    src/grid.hpp
    src/object.hpp
    src/object.cpp
    src/decide_renderer.hpp
    src/input.cpp
    src/input.hpp
    src/scalers.hpp
    src/lru.hpp
    src/qpaintgrid.hpp
    src/qpaintgrid.cpp
    src/animation.hpp
    src/animation.cpp
    src/mouse.hpp
    src/types.hpp
    src/fontdesc.hpp
    src/editor_base.cpp
    src/editor_base.hpp
    src/qt_editorui_base.cpp
    src/qt_editorui_base.hpp
    src/nvim_utils.hpp
    src/qeditor.hpp
    src/qeditor.cpp
  )
  if (WIN32)
    file(GLOB WINONLYTESTSOURCES
      src/platform/windows/*.cpp
      src/platform/windows/*.hpp
    )
    list(APPEND TEST_SOURCES ${WINONLYTESTSOURCES})
  endif()
  add_executable(nvui_test "test/test_main.cpp" ${TEST_SOURCES})
  target_link_libraries(nvui_test PRIVATE Catch2::Catch2)
  target_link_libraries(nvui_test PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Svg)
  target_link_libraries(nvui_test PRIVATE fmt::fmt)
  target_include_directories(nvui_test PRIVATE
    "${PROJECT_SOURCE_DIR}/test"
    "${PROJECT_SOURCE_DIR}/src"
  )
  if (BOOST_FOUND)
    target_include_directories(nvui_test PRIVATE ${Boost_INCLUDE_DIR})
  endif()
  target_link_libraries(nvui_test PRIVATE
    Boost::thread
    Boost::filesystem
  )
  include(CTest)
  include(Catch)
  catch_discover_tests(nvui_test)
endif()
