# CMakeLists.txt --- CMake project settings
##############################################################################

# CMake minimum version
cmake_minimum_required(VERSION 3.6)

# project name and languages
project(cmd_sing VERSION 0.5.1 LANGUAGES CXX)

# UTF-8
if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8")
endif()

##############################################################################
# pevent

add_subdirectory(pevent)

##############################################################################
# fmgon

add_subdirectory(fmgon)

##############################################################################
# cmd_sing

# cmd_sing.exe
add_executable(cmd_sing cmd_sing.cpp cmd_play.cpp sound.cpp main.cpp soundplayer.cpp)
target_compile_definitions(cmd_sing PRIVATE UNICODE _UNICODE JAPAN CMD_SING_EXE)
target_link_libraries(cmd_sing fmgon shlwapi winmm)

##############################################################################
