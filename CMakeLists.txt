# /mnt/d/visual studio/GSL-2.0.0/build

cmake_minimum_required(VERSION 3.5.0)
project(directory_creator VERSION 0.1.0 LANGUAGES CXX)

find_package(Microsoft.GSL REQUIRED)
find_package(Boost REQUIRED)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXE_LINKER_FLAGS -fuse-ld=gold)

add_executable(simple_menu simple_menu.cpp)

target_link_libraries(simple_menu PRIVATE -lstdc++fs -lpthread -lncurses -lmenu)
target_link_libraries(simple_menu PRIVATE Microsoft.GSL::GSL)
target_include_directories(simple_menu PRIVATE ${Boost_INCLUDE_DIRS})
target_compile_options(simple_menu PRIVATE -Wall -O3 -march=native)

install(TARGETS simple_menu
        RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX})