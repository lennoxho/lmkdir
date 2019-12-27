# /mnt/d/visual studio/GSL-2.0.0/build
# /mnt/d/boost_1_70_0/linux64/rel/include

cmake_minimum_required(VERSION 3.5.0)
project(directory_creator VERSION 0.1.0 LANGUAGES CXX)

find_package(Microsoft.GSL REQUIRED)
find_package(Boost REQUIRED)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXE_LINKER_FLAGS -fuse-ld=gold)

add_executable(lmkdir lmkdir.cpp lmkdir_errors.cpp)
add_executable(simple_menu simple_menu.cpp lmkdir_errors.cpp)

target_link_libraries(lmkdir PRIVATE -lstdc++fs -lncurses -lmenu -lboost_regex)
target_link_libraries(lmkdir PRIVATE Microsoft.GSL::GSL)
target_include_directories(lmkdir PRIVATE ${Boost_INCLUDE_DIRS})
target_link_directories(lmkdir PRIVATE ${Boost_LIBRARY_DIRS})

target_link_libraries(simple_menu PRIVATE -lstdc++fs -lncurses -lmenu)
target_link_libraries(simple_menu PRIVATE Microsoft.GSL::GSL)
target_include_directories(simple_menu PRIVATE ${Boost_INCLUDE_DIRS})

install(TARGETS lmkdir
        RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX})
install(TARGETS simple_menu
        RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX})