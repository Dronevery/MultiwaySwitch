cmake_minimum_required(VERSION 2.8.4)
project(MultiSitch C CXX)

set(CMAKE_CXX_FLAGS "-std=c++0x ${CMAKE_CXX_FLAGS} -g -ftest-coverage -fprofile-arcs")

find_package (Threads)


set(SOURCE_FILES_CLIENT src/client.cpp)
set(SOURCE_FILES_SERVER src/server.cpp)

add_executable(multiswitchclient ${SOURCE_FILES_CLIENT} src/config.h src/config.h src/config.cpp src/inih/cpp/INIReader.h src/inih/cpp/INIReader.cpp src/inih/ini.h src/inih/ini.c)
add_executable(multiswitchserver ${SOURCE_FILES_SERVER} src/config.h src/config.h src/config.cpp src/inih/cpp/INIReader.h src/inih/cpp/INIReader.cpp src/inih/ini.h src/inih/ini.c)

target_link_libraries (multiswitchclient ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries (multiswitchserver ${CMAKE_THREAD_LIBS_INIT})

set_target_properties(multiswitchclient PROPERTIES LINKER_LANGUAGE CXX)
set_target_properties(multiswitchserver PROPERTIES LINKER_LANGUAGE CXX)

install(TARGETS multiswitchclient DESTINATION bin)
install(TARGETS multiswitchserver DESTINATION bin)

install(FILES config/client.ini DESTINATION /etc/multiswitch COMPONENT config)
install(FILES config/server.ini DESTINATION /etc/multiswitch COMPONENT config)
