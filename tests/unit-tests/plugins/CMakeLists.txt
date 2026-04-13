cmake_minimum_required(VERSION 3.16)
project(TestPlugins LANGUAGES CXX)

add_executable(motionplayer-dll motionplayer-dll.cpp main.cpp)

set(_KRKR2PLUGIN_LINK_ITEM "$<LINK_LIBRARY:WHOLE_ARCHIVE,krkr2plugin>")

target_link_libraries(motionplayer-dll
    PRIVATE
        Catch2::Catch2
    PUBLIC
        krkr2core
        ${_KRKR2PLUGIN_LINK_ITEM}
)

target_include_directories(motionplayer-dll PRIVATE "${TEST_CONFIG_DIR}")

catch_discover_tests(motionplayer-dll)
