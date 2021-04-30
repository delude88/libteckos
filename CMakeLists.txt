cmake_minimum_required(VERSION 3.16.0)

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
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/FixAppleArchitecture.cmake)
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/BrewResolver.cmake)
endif (APPLE)

add_compile_definitions(DEBUG)
find_package(nlohmann_json 3.2.0 REQUIRED)
find_package(cpprestsdk REQUIRED)
find_package(OpenSSL REQUIRED)

set(PUBLIC_HEADER
        ${CMAKE_CURRENT_SOURCE_DIR}/include/teckos/client.h)
set(SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/teckos/client.cc)
add_library(teckos ${SOURCES})
target_include_directories(teckos PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(teckos INTERFACE -Wall -Wextra -pedantic -Werror)
set_target_properties(teckos PROPERTIES PUBLIC_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/include/teckos/client.h)
target_link_libraries(teckos
        PUBLIC
        nlohmann_json::nlohmann_json
        OpenSSL::Crypto
        cpprestsdk::cpprest)

add_executable(cli "${CMAKE_CURRENT_SOURCE_DIR}/src/cli.cc")
target_include_directories(cli PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(cli
        PUBLIC
        teckos)

include(GNUInstallDirs)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/teckos DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS teckos
        PUBLIC_HEADER
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/DigitalStage
        )