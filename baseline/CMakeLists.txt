cmake_minimum_required(VERSION 3.16)

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

# MAC layer
add_library(sdr_mac STATIC
    src/mac/packet_builder.cpp
    src/mac/packet_parser.cpp
    src/mac/validation.cpp
)

target_include_directories(sdr_mac
    PUBLIC
        ${PROJECT_INCLUDE_DIR}
)

target_link_libraries(sdr_mac
    PUBLIC
        #sdr_common
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
        #sdr_common
)


# Transmitter
add_executable(sdr_tx
    src/main_tx.cpp
)

target_link_libraries(sdr_tx
    PRIVATE
        #sdr_common
        sdr_mac
        sdr_phy
        Boost::program_options
)

# Receiver
add_executable(sdr_rx
    src/main_rx.cpp
)

target_link_libraries(sdr_rx
    PRIVATE
        #sdr_common
        sdr_mac
        sdr_phy
        Boost::program_options
)


if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    #target_compile_options(sdr_common PRIVATE
    #    -Wall
    #    -Wextra
    #    -Wpedantic
    #)

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
