cmake_minimum_required(VERSION 2.8.4)
project(MultiSitch)

set(SOURCE_FILES_CLIENT src/client.cpp)
set(SOURCE_FILES_SERVER src/server.cpp)
add_executable(client ${SOURCE_FILES_CLIENT})
add_executable(server ${SOURCE_FILES_SERVER})
