cmake_minimum_required(VERSION 3.16.0)

if (DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
            CACHE STRING "")
elseif (DEFINED ENV{VCPKG_INSTALLATION_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_INSTALLATION_ROOT}/scripts/buildsystems/vcpkg.cmake"
            CACHE STRING "")
endif ()

project(teckos
        LANGUAGES CXX
        VERSION 0.1
        DESCRIPTION "Header only C++ client library for Teckos"
        )

##
## CONFIGURATION
##
include(GNUInstallDirs)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(TARGET_NAME ${PROJECT_NAME})
set(TARGETS_EXPORT_NAME "${PROJECT_NAME}Targets")
set(INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)
set(INCLUDE_INSTALL_DIR ${CMAKE_INSTALL_INCLUDEDIR} CACHE PATH "Install path for include files")
set(LIB_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR} CACHE PATH "Install path for library files")

option(TECKOS_BUILD_SOURCES "Build dependencies from sources (e.g. for MacOS Universal build)" OFF)


##
## DEPENDENCIES
##
if (APPLE)
    message(STATUS "Applying hotfixes for macOS")
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/FixAppleArchitecture.cmake)
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/BrewResolver.cmake)
endif (APPLE)

## Prepare use of Conan
list(APPEND CMAKE_MODULE_PATH ${CMAKE_BINARY_DIR})

if (TECKOS_BUILD_SOURCES)
    # nlohmann_json::nlohmann_json
    set(JSON_BuildTests OFF CACHE INTERNAL "")
    include(FetchContent)
    FetchContent_Declare(json
            GIT_REPOSITORY https://github.com/ArthurSonzogni/nlohmann_json_cmake_fetchcontent
            GIT_TAG v3.10.4)
    FetchContent_GetProperties(json)
    if (NOT json_POPULATED)
        FetchContent_Populate(json)
        add_subdirectory(${json_SOURCE_DIR} ${json_BINARY_DIR} EXCLUDE_FROM_ALL)
    endif ()
    # boost::*
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/deps/boost-cmake)
    # cpprestsdk
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/deps/cpprestsdk)
    # openssl, crypto
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/deps/openssl-cmake)
else ()
    find_package(cpprestsdk REQUIRED)
    find_package(OpenSSL REQUIRED)
    find_package(nlohmann_json REQUIRED)
endif ()


##
## TARGET
## create target and add include path
##
add_library(teckos ${PROJECT_SOURCE_DIR}/src/teckos/client.cc)
add_library(teckosStatic STATIC ${PROJECT_SOURCE_DIR}/src/teckos/client.cc)
add_library(${PROJECT_NAME}::teckos ALIAS teckos)
add_library(${PROJECT_NAME}::teckosStatic ALIAS teckosStatic)
target_include_directories(teckos PUBLIC
        $<BUILD_INTERFACE:${INCLUDE_DIR}>
        $<INSTALL_INTERFACE:include>)
target_include_directories(teckosStatic PUBLIC
        $<BUILD_INTERFACE:${INCLUDE_DIR}>
        $<INSTALL_INTERFACE:include>)
if (CMAKE_COMPILER_IS_GNUCC)
    target_compile_options(teckos PRIVATE -Wall -Wextra -pedantic -Werror)
    target_compile_options(teckosStatic PRIVATE -Wall -Wextra -pedantic -Werror)
endif ()

if (TECKOS_BUILD_SOURCES)
    set(TECKOS_LINK_LIBRARIES
            Boost::system
            Boost::thread
            Boost::log
            Boost::program_options
            Boost::chrono
            thirdparty_openssl_ssl
            thirdparty_openssl_crypto
            cpprest
            )
else ()
    set(TECKOS_LINK_LIBRARIES
            nlohmann_json::nlohmann_json
            OpenSSL::Crypto
            cpprestsdk::cpprest
            )
endif ()
target_link_libraries(teckos
        PUBLIC
        ${TECKOS_LINK_LIBRARIES})
target_link_libraries(teckosStatic
        PUBLIC
        ${TECKOS_LINK_LIBRARIES})

add_executable(teckos-example EXCLUDE_FROM_ALL "${CMAKE_CURRENT_SOURCE_DIR}/src/cli.cc")
set_target_properties(teckos-example PROPERTIES OUTPUT_NAME cli)
target_include_directories(teckos-example PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(teckos-example
        PUBLIC
        teckos)


##
## INSTALL
## install header files, generate and install cmake config files for find_package()
##
include(CMakePackageConfigHelpers)
configure_package_config_file(${PROJECT_SOURCE_DIR}/cmake/Config.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
        INSTALL_DESTINATION ${LIB_INSTALL_DIR}/${PROJECT_NAME}/cmake
        PATH_VARS INCLUDE_INSTALL_DIR)
write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
        DESTINATION ${LIB_INSTALL_DIR}/cmake/${PROJECT_NAME})
install(
        DIRECTORY ${INCLUDE_DIR}/
        DESTINATION ${INCLUDE_INSTALL_DIR}
)
export(
        TARGETS teckos teckosStatic
        NAMESPACE ${PROJECT_NAME}::
        FILE "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Targets.cmake")
install(
        TARGETS teckos teckosStatic
        EXPORT ${TARGETS_EXPORT_NAME}
        INCLUDES DESTINATION ${INCLUDE_INSTALL_DIR})
install(
        EXPORT ${TARGETS_EXPORT_NAME}
        NAMESPACE ${PROJECT_NAME}::
        DESTINATION ${LIB_INSTALL_DIR}/cmake/${PROJECT_NAME})