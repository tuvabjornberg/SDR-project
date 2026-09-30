cmake_minimum_required(VERSION 3.16)

project(SDRProject
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(UHD 4.9.0 REQUIRED)

find_package(Boost 1.66 REQUIRED COMPONENTS
    program_options
)

message(STATUS "UHD version: ${UHD_VERSION}")
message(STATUS "UHD libraries: ${UHD_LIBRARIES}")
message(STATUS "UHD include directories: ${UHD_INCLUDE_DIRS}")

set(PROJECT_INCLUDE_DIR
    ${PROJECT_SOURCE_DIR}/include
)

# MAC layer
add_library(sdr_mac STATIC
    src/mac/packet_builder.cpp
    src/mac/packet_parser.cpp
    src/mac/crc.cpp
)

target_include_directories(sdr_mac
    PUBLIC
        ${PROJECT_INCLUDE_DIR}
)

target_link_libraries(sdr_mac
    PUBLIC
        sdr_common
)

# PHY layer
add_library(sdr_phy STATIC
    src/phy/modulator.cpp
    src/phy/demodulator.cpp
    src/phy/synchronizer.cpp
    src/phy/pulse_shaper.cpp
    src/phy/matched_filter.cpp
)

target_include_directories(sdr_phy
    PUBLIC
        ${PROJECT_INCLUDE_DIR}
)

target_link_libraries(sdr_phy
    PUBLIC
        sdr_common
)

# Radio / UHD layer
add_library(sdr_radio STATIC
    src/radio/usrp_tx.cpp
    src/radio/usrp_rx.cpp
)

target_include_directories(sdr_radio
    PUBLIC
        ${PROJECT_INCLUDE_DIR}
        ${UHD_INCLUDE_DIRS}
)

target_link_libraries(sdr_radio
    PUBLIC
        ${UHD_LIBRARIES}
)

# Transmitter
add_executable(sdr_tx
    src/main_tx.cpp
)

target_link_libraries(sdr_tx
    PRIVATE
        sdr_common
        sdr_mac
        sdr_phy
        sdr_radio
        Boost::program_options
)

# Receiver
add_executable(sdr_rx
    src/main_rx.cpp
)

target_link_libraries(sdr_rx
    PRIVATE
        sdr_common
        sdr_mac
        sdr_phy
        sdr_radio
        Boost::program_options
)


if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(sdr_common PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

    target_compile_options(sdr_mac PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

    target_compile_options(sdr_phy PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

    target_compile_options(sdr_radio PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

    target_compile_options(sdr_tx PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

    target_compile_options(sdr_rx PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

install(TARGETS
    sdr_tx
    sdr_rx
    RUNTIME DESTINATION bin
)
