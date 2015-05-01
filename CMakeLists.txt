cmake_minimum_required(VERSION 2.8.4)
project(MultiSitch C CXX)

set(SOURCE_FILES_CLIENT src/client.cpp)
set(SOURCE_FILES_SERVER src/server.cpp)

add_executable(client ${SOURCE_FILES_CLIENT} src/config.h src/config.h src/config.cpp src/inih/cpp/INIReader.h src/inih/cpp/INIReader.cpp src/inih/ini.h src/inih/ini.c)
add_executable(server ${SOURCE_FILES_SERVER} src/config.h src/config.h src/config.cpp src/inih/cpp/INIReader.h src/inih/cpp/INIReader.cpp src/inih/ini.h src/inih/ini.c)

set_target_properties(client PROPERTIES LINKER_LANGUAGE CXX)
set_target_properties(server PROPERTIES LINKER_LANGUAGE CXX)