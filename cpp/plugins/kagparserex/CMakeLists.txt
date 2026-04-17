cmake_minimum_required(VERSION 3.19)
project(kagparserex)

add_library(${PROJECT_NAME} STATIC)

target_sources(${PROJECT_NAME} PRIVATE
    kagparserex_plugin.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE krkr2plugin)
