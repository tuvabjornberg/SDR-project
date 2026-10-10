cmake_minimum_required(VERSION 3.16)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(SDRProject
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)

if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD" AND ${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
    set(CMAKE_EXE_LINKER_FLAGS "-lthr ${CMAKE_EXE_LINKER_FLAGS}")
    set(CMAKE_CXX_FLAGS "-stdlib=libc++ ${CMAKE_CXX_FLAGS}")
endif()

### Set up build environment ##################################################
# Choose a static or shared-library build (shared is default, and static will
# probably need some special care!)
# Set this to ON in order to link a static build of UHD:
option(UHD_USE_STATIC_LIBS OFF)

find_package(UHD 4.9.0 REQUIRED)

include(UHDMinDepVersions)

set(UHD_BOOST_REQUIRED_COMPONENTS
    program_options
)
include(UHDBoost)

#find_package(Boost 1.66 REQUIRED COMPONENTS
#    program_options
#)

message(STATUS "UHD version: ${UHD_VERSION}")
message(STATUS "UHD libraries: ${UHD_LIBRARIES}")
message(STATUS "UHD include directories: ${UHD_INCLUDE_DIRS}")

set(PROJECT_INCLUDE_DIR
    ${PROJECT_SOURCE_DIR}/include
)

include_directories(${CMAKE_SOURCE_DIR})

# MAC layer
add_library(mac STATIC
    src/mac/packet_builder.cpp
    src/mac/packet_parser.cpp
    src/mac/validation.cpp
)

target_include_directories(mac
    PUBLIC
        ${PROJECT_INCLUDE_DIR}
)

target_link_libraries(mac
    PUBLIC
)

# PHY layer
add_library(phy STATIC
    src/phy/modulator.cpp
    src/phy/demodulator.cpp
    src/phy/synchronizer.cpp
    src/phy/pulse_shaper.cpp
    src/phy/filter.cpp
)

target_include_directories(phy
    PUBLIC
        ${PROJECT_INCLUDE_DIR}
)

target_link_libraries(phy
    PUBLIC
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
)

# Transmitter
add_executable(sdr_tx
    src/main_tx.cpp
)

target_link_libraries(sdr_tx
    PRIVATE
        mac
        phy
        radio
)

# Receiver
add_executable(sdr_rx
    src/main_rx.cpp
)

target_link_libraries(sdr_rx
    PRIVATE
        mac
        phy
        radio
)

# TX/RX combination
add_executable(sdr_txrx
    src/main_txrx.cpp
)

target_link_libraries(sdr_txrx
    PRIVATE
        mac
        phy
        radio
)


if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")

    target_compile_options(mac PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

    target_compile_options(phy PRIVATE
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

# Tests
option(BUILD_TESTS "Build tests" ON)

if(BUILD_TESTS)

    enable_testing()

    add_executable(test_modulator
        tests/test_modulator.cpp
    )

    target_link_libraries(test_modulator
        PRIVATE
            phy
    )

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")

        target_compile_options(test_modulator PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )

    endif()

    add_test(
        NAME modulator
        COMMAND test_modulator
    )

endif()

