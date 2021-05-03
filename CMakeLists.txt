cmake_minimum_required(VERSION 3.2)
if(MSVC)
  add_compile_options(/W4 /WX)
else()
  add_compile_options(-Wall -Wextra -pedantic)
endif()
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2")
project(nvui)
set(PACKAGE_DIR $ENV(CPP_PACKAGE_DIR))
set(CMAKE_CXX_STANDARD 20)
find_package(msgpack CONFIG REQUIRED)
find_package(Catch2 CONFIG REQUIRED)
file(GLOB_RECURSE SOURCES
  src/*.h
  src/*.hpp
  src/*.cpp
)
find_package(Boost COMPONENTS filesystem thread REQUIRED)
add_executable(nvui "src/main.cpp" ${SOURCES})
target_link_libraries(nvui PRIVATE msgpackc msgpackc-cxx)
target_link_libraries(nvui PRIVATE Catch2::Catch2)
target_include_directories(nvui PRIVATE
  "${PROJECT_SOURCE_DIR}/src"
)
if (Boost_FOUND)
  target_include_directories(nvui PRIVATE ${Boost_INCLUDE_DIR})
endif()
target_link_libraries(${PROJECT_NAME} PRIVATE
  ${Boost_FILESYSTEM_LIBRARY}
  ${Boost_THREAD_LIBRARY}
  ${Boost_LIBRARIES}
)

file(GLOB TEST_SOURCES
  src/nvim.hpp
  src/nvim.cpp
  test/*.cpp
)
add_executable(nvui_test "test/test_main.cpp" ${TEST_SOURCES})
target_link_libraries(nvui_test PRIVATE msgpackc msgpackc-cxx)
target_link_libraries(nvui_test PRIVATE Catch2::Catch2)
target_include_directories(nvui_test PRIVATE
  "${PROJECT_SOURCE_DIR}/test"
  "${PROJECT_SOURCE_DIR}/src"
)
if (BOOST_FOUND)
  target_include_directories(nvui_test PRIVATE ${Boost_INCLUDE_DIR})
endif()
target_link_libraries(nvui_test PRIVATE
  ${Boost_FILESYSTEM_LIBRARY}
  ${Boost_THREAD_LIBRARY}
  ${Boost_LIBRARIES}
)
include(CTest)
include(Catch)
catch_discover_tests(nvui_test)
