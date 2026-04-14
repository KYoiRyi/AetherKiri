cmake_minimum_required(VERSION 3.19)
project(psbfile)

add_library(${PROJECT_NAME} STATIC)

target_sources(${PROJECT_NAME} PRIVATE
    main.cpp
    PSBValue.cpp
    PSBFile.cpp
    PSBMedia.cpp
    PSBMediaRegistry.cpp
    types/PimgType.cpp
    types/MotionType.cpp
    types/PimgType.cpp
    types/ScnType.cpp
    types/ImageType.cpp
    types/MmoType.cpp
    types/ArchiveType.cpp
    types/SoundArchiveType.cpp
    resources/ImageMetadata.cpp
)

find_package(ZLIB REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE krkr2plugin ZLIB::ZLIB)
