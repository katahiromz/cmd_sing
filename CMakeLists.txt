# CMakeLists.txt --- CMake project settings
##############################################################################

# CMake minimum version
cmake_minimum_required(VERSION 3.6)

# project name, version, and languages
project(cmd_sing VERSION 1.3 LANGUAGES CXX RC)

# UTF-8
if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8")
endif()

##############################################################################
# CMake options

option(ENABLE_BEEP "Enable beep" ON)

##############################################################################
# pevent

add_subdirectory(pevent)

##############################################################################
# fmgon

add_subdirectory(fmgon)

##############################################################################
# cmd_sing

# cmd_sing.exe
add_executable(cmd_sing cmd_sing.cpp cmd_play.cpp sound.cpp main.cpp soundplayer.cpp cmd_sing_res.rc)
target_compile_definitions(cmd_sing PRIVATE UNICODE _UNICODE JAPAN CMD_SING_EXE)
target_link_libraries(cmd_sing fmgon shlwapi winmm)
if(ENABLE_BEEP)
    target_compile_definitions(cmd_sing PRIVATE ENABLE_BEEP)
endif()

##############################################################################
