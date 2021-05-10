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
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(TARGET_NAME ${PROJECT_NAME})
set(TARGETS_EXPORT_NAME "${PROJECT_NAME}Targets")
set(INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)
set(INCLUDE_INSTALL_DIR ${CMAKE_INSTALL_INCLUDEDIR} CACHE PATH "Install path for include files")
set(LIB_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR} CACHE PATH "Install path for library files")


##
## DEPENDENCIES
##
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


##
## TARGET
## create target and add include path
##
add_library(teckos ${PROJECT_SOURCE_DIR}/src/teckos/client.cc)
add_library(${PROJECT_NAME}::teckos ALIAS teckos)
target_include_directories(teckos PUBLIC
        $<BUILD_INTERFACE:${INCLUDE_DIR}>
        $<INSTALL_INTERFACE:include>)
target_compile_options(teckos PRIVATE -Wall -Wextra -pedantic -Werror)
target_link_libraries(teckos
        PUBLIC
        nlohmann_json::nlohmann_json
        OpenSSL::Crypto
        cpprestsdk::cpprest)

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
        TARGETS teckos
        NAMESPACE ${PROJECT_NAME}::
        FILE "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Targets.cmake")
install(
        TARGETS teckos
        EXPORT ${TARGETS_EXPORT_NAME}
        INCLUDES DESTINATION ${INCLUDE_INSTALL_DIR})
install(
        EXPORT ${TARGETS_EXPORT_NAME}
        NAMESPACE ${PROJECT_NAME}::
        DESTINATION ${LIB_INSTALL_DIR}/cmake/${PROJECT_NAME})