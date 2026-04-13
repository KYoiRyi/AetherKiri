cmake_minimum_required(VERSION 3.16)
project(TestPlugins LANGUAGES CXX)

add_executable(motionplayer-dll motionplayer-dll.cpp main.cpp)

target_link_libraries(motionplayer-dll
    PRIVATE
        Catch2::Catch2
    PUBLIC
        krkr2plugin
        krkr2core
)

target_include_directories(motionplayer-dll PRIVATE "${TEST_CONFIG_DIR}")

catch_discover_tests(motionplayer-dll)
