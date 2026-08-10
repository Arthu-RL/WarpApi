cmake_minimum_required(VERSION 3.16)

project(WarpApi
    LANGUAGES CXX
    VERSION 1.0.0
)

# libink requires C++23 (std::expected, std::source_location, stringstream::view)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++ -static-libgcc")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(USE_EPOLL "Build API with EPOLL request processing system" ON)
option(USE_IOURING "Build API with IO_URING request processing system" OFF)
option(WARP_NATIVE_ARCH "Tune the binary for the building machine (-march=native)" ON)
option(WARP_LTO "Enable link time optimization on non-Debug builds" ON)

if((USE_EPOLL AND USE_IOURING) OR (NOT USE_EPOLL AND NOT USE_IOURING))
    message(FATAL_ERROR "You can't use both backends, but you need to use at least one of those")
endif()

# Add library paths
list(APPEND CMAKE_PREFIX_PATH "$ENV{LIBRARY_PATH}")

set(ALL_LIBRARY_PATH "$ENV{LIBRARY_PATH}/lib")

# Find required packages
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

find_library(INK_LIB ink HINTS ${ALL_LIBRARY_PATH} REQUIRED)

# liburing is only a dependency of the io_uring backend
if(USE_IOURING)
    find_file(URING_STATIC_LIB
        NAMES liburing.a
        PATHS /usr/lib /usr/lib/x86_64-linux-gnu /usr/local/lib /lib
        REQUIRED
    )
else()
    set(URING_STATIC_LIB "")
endif()

# Collect source files
file(GLOB_RECURSE SOURCES
    ${CMAKE_SOURCE_DIR}/src/*.cpp
    ${CMAKE_SOURCE_DIR}/src/*.h
)

# Define executable target
add_executable(${PROJECT_NAME}
    ${SOURCES}
)

# Define the compile defs of request processing systems
target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<BOOL:${USE_EPOLL}>:USE_EPOLL>
    $<$<BOOL:${USE_IOURING}>:USE_IOURING>
)

# Compiler flags
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(${PROJECT_NAME} PRIVATE -g -O0 -Wall -Wextra -Wpedantic)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE
        -O3
        -fno-rtti
        -fno-plt
        -ffunction-sections
        -fdata-sections
        -fomit-frame-pointer
        # The binary is a self-contained executable, so nothing can interpose
        # our symbols; this lets GCC inline and constant-propagate across the
        # whole program instead of routing calls through the PLT.
        -fno-semantic-interposition
        -fvisibility=hidden
        -fvisibility-inlines-hidden
        # Hot loops land on cache-line boundaries rather than straddling them.
        -falign-functions=32
        -Wall
        -Wextra
    )
    target_link_options(${PROJECT_NAME} PRIVATE
        -Wl,--gc-sections
        # Resolve all relocations at load time so the hot path never takes a
        # lazy-binding detour, and make the GOT read-only afterwards.
        -Wl,-O2
        -Wl,--as-needed
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE NDEBUG)

    if(WARP_NATIVE_ARCH)
        include(CheckCXXCompilerFlag)
        check_cxx_compiler_flag("-march=native" HAS_MARCH_NATIVE)
        if(HAS_MARCH_NATIVE)
            target_compile_options(${PROJECT_NAME} PRIVATE -march=native -mtune=native)
        endif()
    endif()

    # LTO has to be applied to compile *and* link steps; let CMake handle it.
    if(WARP_LTO)
        include(CheckIPOSupported)
        check_ipo_supported(RESULT WARP_IPO_OK OUTPUT WARP_IPO_MSG)
        if(WARP_IPO_OK)
            set_property(TARGET ${PROJECT_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
        else()
            message(WARNING "LTO requested but unavailable: ${WARP_IPO_MSG}")
        endif()
    endif()
endif()

set(APP_CONFIGS "${CMAKE_SOURCE_DIR}/config.json")
set(APP_CONFIGS_DEST "${CMAKE_BINARY_DIR}")

add_custom_target(copy_app_configs ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_CONFIGS_DEST}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${APP_CONFIGS}"
        "${APP_CONFIGS_DEST}/config.json"
)
add_dependencies(${PROJECT_NAME} copy_app_configs)

add_definitions(-DCMAKE_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\")

# Include directories for the main executable
target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    "$ENV{LIBRARY_PATH}/include"
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    Threads::Threads
    OpenSSL::SSL
    ${URING_STATIC_LIB}
    ${INK_LIB}
    dl
)

# Installation settings
include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
