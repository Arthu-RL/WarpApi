cmake_minimum_required(VERSION $ENV{CMAKE_VERSION})

project(CustomHPAPI
    LANGUAGES CXX
    VERSION 1.0.0
)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add library paths
list(APPEND CMAKE_PREFIX_PATH "$ENV{LIBRARY_PATH}")

# # Find required packages
find_package(Threads REQUIRED)
find_package(Boost REQUIRED)

# Set paths for third-party libraries
set(PLOG_PATH "$ENV{LIBRARY_PATH}/include/plog")

# Collect source files
file(GLOB_RECURSE SOURCES
    ${CMAKE_SOURCE_DIR}/src/*.cpp
    ${CMAKE_SOURCE_DIR}/src/*.h
)

# Define executable target
add_executable(${PROJECT_NAME}
    ${SOURCES}
)

add_definitions(-DCMAKE_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\")

# Include directories for the main executable
target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    "$ENV{LIBRARY_PATH}/include"
    ${PLOG_PATH}
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PUBLIC
    Threads::Threads
    dl
)

# Installation settings
include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
