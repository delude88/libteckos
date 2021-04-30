cmake_minimum_required(VERSION 3.16.0)

if (DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
            CACHE STRING "")
endif ()

### GLOBAL CONFIGURATION
project(TeckosClient
        LANGUAGES CXX
        VERSION 0.1
        DESCRIPTION "Header only C++ client library for Teckos"
        )
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# HOTFIXES
if (APPLE)
    message(STATUS "Applying hotfixes for macOS")
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/FixAppleArchitecture.cmake)
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/BrewResolver.cmake)
endif (APPLE)

if (WIN32)
    find_package(nlohmann_json REQUIRED)
else ()
    find_package(nlohmann_json 3.2.0 REQUIRED)
endif ()
find_package(cpprestsdk REQUIRED)
find_package(OpenSSL REQUIRED)

add_library(teckos STATIC ${CMAKE_CURRENT_SOURCE_DIR}/src/teckos/client.cc)
target_include_directories(teckos PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(teckos INTERFACE -Wall -Wextra -pedantic -Werror)
set_target_properties(teckos PROPERTIES PUBLIC_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/include/teckos/client.h)
target_link_libraries(teckos
        PUBLIC
        nlohmann_json::nlohmann_json
        OpenSSL::Crypto
        cpprestsdk::cpprest)

add_executable(cli EXCLUDE_FROM_ALL "${CMAKE_CURRENT_SOURCE_DIR}/src/cli.cc")
target_include_directories(cli PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(cli
        PUBLIC
        teckos)

include(GNUInstallDirs)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/teckos DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS teckos
        PUBLIC_HEADER
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/teckos
        )
