cmake_minimum_required(VERSION 3.16)

project(SDRProject
    VERSION 0.1.0
    DESCRIPTION "Software Defined Radio Project"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(UHD_USE_STATIC_LIBS "Link against static UHD libraries" OFF)
option(BUILD_APPS "Build SDR applications" ON)
option(BUILD_TESTS "Build tests" ON)


if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD"
   AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")

    add_compile_options(-stdlib=libc++)
    add_link_options(-stdlib=libc++)

endif()

find_package(UHD 4.9.0 REQUIRED)
find_package(JPEG REQUIRED)

include(UHDMinDepVersions)

set(UHD_BOOST_REQUIRED_COMPONENTS
    program_options
)

include(UHDBoost)

find_package(Threads REQUIRED)

message(STATUS "UHD version: ${UHD_VERSION}")
message(STATUS "UHD libraries: ${UHD_LIBRARIES}")

add_library(project_warnings INTERFACE)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(project_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

set(PROJECT_INCLUDE_DIR
    ${PROJECT_SOURCE_DIR}/include
)

# MAC layer
add_library(mac STATIC
    src/mac/packet.cpp
    src/mac/validation.cpp
)

target_include_directories(mac
    PUBLIC
        ${PROJECT_INCLUDE_DIR}
)

target_link_libraries(mac
    PRIVATE
        project_warnings
)

# PHY layer
add_library(phy STATIC
    src/phy/modulator.cpp
    src/phy/synchronizer.cpp
    src/phy/filter.cpp
)

target_include_directories(phy
    PUBLIC
        ${PROJECT_INCLUDE_DIR}
)

target_link_libraries(phy
    PRIVATE
        project_warnings
)

# Radio layer
add_library(radio STATIC
    src/radio/transmitter.cpp
    src/radio/receiver.cpp
)

target_include_directories(radio
    PUBLIC
        ${PROJECT_INCLUDE_DIR}
        ${UHD_INCLUDE_DIRS}
)

target_link_libraries(radio
    PUBLIC
        ${UHD_LIBRARIES}
        ${UHD_STATIC_LIB_LINK_FLAG}
        ${UHD_STATIC_LIB_DEPS}
        Boost::program_options
        Threads::Threads

    PRIVATE
        project_warnings
)

# Pipeline layer
add_library(pipeline STATIC
    src/pipeline/tx_pipeline.cpp
    src/pipeline/rx_pipeline.cpp
)

target_include_directories(pipeline
    PUBLIC
        ${PROJECT_INCLUDE_DIR}
)

target_link_libraries(pipeline
    PUBLIC
        mac
        phy
        JPEG::JPEG

    PRIVATE
        project_warnings
)

if(BUILD_APPS)
    add_executable(transmitter
        apps/transmitter_main.cpp
    )

    target_link_libraries(transmitter
        PRIVATE
            radio
            pipeline
            project_warnings
    )

    add_executable(receiver
        apps/receiver_main.cpp
    )

    target_link_libraries(receiver
        PRIVATE
            radio
            pipeline
            project_warnings
    )

    add_executable(txrx
        apps/txrx_main.cpp
    )

    target_link_libraries(txrx
        PRIVATE
            radio
            pipeline
            project_warnings
    )

endif()

if(BUILD_TESTS)

    enable_testing()

    add_executable(tone_test
        tests/tone/mimo_tone_test.cpp
    )

    target_link_libraries(tone_test
        PRIVATE
            radio
            pipeline
            project_warnings
    )

    add_test(
        NAME tone_test
        COMMAND tone_test
    )

endif()
