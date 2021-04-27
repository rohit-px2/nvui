cmake_minimum_required(VERSION 3.2)
project(nvui)
set(PACKAGE_DIR $ENV(CPP_PACKAGE_DIR))
set(CMAKE_CXX_STANDARD 17)
find_package(msgpack CONFIG REQUIRED)
file(GLOB_RECURSE SOURCES
  src/*.h
  src/*.cpp
)
find_package(Boost COMPONENTS filesystem thread REQUIRED)
add_executable(nvui "src/main.cpp" ${SOURCES})
target_link_libraries(nvui PRIVATE msgpackc msgpackc-cxx)
target_include_directories(nvui PRIVATE
  "${PROJECT_SOURCE_DIR}/src"
)
if (Boost_FOUND)
  include_directories(${Boost_INCLUDE_DIR})
endif()
if(MSVC)
  add_compile_options(/W4 /WX)
else()
  add_compile_options(-Wall -Wextra -Werror -pedantic)
endif()
target_link_libraries(${PROJECT_NAME} PRIVATE
  ${Boost_FILESYSTEM_LIBRARY}
  ${Boost_THREAD_LIBRARY}
  ${Boost_LIBRARIES}
)
