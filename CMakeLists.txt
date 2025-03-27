cmake_minimum_required(VERSION $ENV{CMAKE_VERSION})

project(WarpApi
    LANGUAGES CXX
    VERSION 1.0.0
)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++ -static-libgcc")

# Add library paths
list(APPEND CMAKE_PREFIX_PATH "$ENV{LIBRARY_PATH}")

set(ALL_LIBRARY_PATH "$ENV{LIBRARY_PATH}/lib")

# # Find required packages
find_package(Threads REQUIRED)
find_library(TBB_STATIC_LIB NAMES libtbb.a tbb HINTS ${ALL_LIBRARY_PATH})
find_library(INK_LIB ink HINTS ${ALL_LIBRARY_PATH})

if(NOT TBB_STATIC_LIB)
    message(FATAL_ERROR "TBB static library (libtbb.a) not found!")
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
    ${TBB_STATIC_LIB}
    ${INK_LIB}
    dl
)

# Installation settings
include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
