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


#################################################
#
#   Common configuration
#
#################################################
include(GNUInstallDirs)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_BINARY_DIR})
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules)
set(TARGET_NAME ${PROJECT_NAME})
set(TARGETS_EXPORT_NAME "${PROJECT_NAME}Targets")
set(INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)
set(INCLUDE_INSTALL_DIR ${CMAKE_INSTALL_INCLUDEDIR} CACHE PATH "Install path for include files")
set(LIB_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR} CACHE PATH "Install path for library files")
if (APPLE)
    if (EXISTS "/usr/local/opt/openssl/")
        set(OPENSSL_ROOT_DIR "/usr/local/opt/openssl/")
    else ()
        include("${PROJECT_SOURCE_DIR}/cmake/BrewResolver.cmake")
    endif ()
endif (APPLE)


#################################################
#
#   Environment
#
#################################################
option(USE_IX_WEBSOCKET "Use ixwebsocket instead of cpprestsdk" OFF)


#################################################
#
#   Dependencies
#
#################################################
include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake OPTIONAL)
if (COMMAND conan_basic_setup)
    conan_basic_setup()
endif ()
if (USE_IX_WEBSOCKET)
    set(USE_TLS ON CACHE BOOL "Support TLS" FORCE)
    set(BUILD_SHARED_LIBS ON CACHE BOOL "Don't use static libs" FORCE)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/IXWebSocket EXCLUDE_FROM_ALL)
else ()
    find_package(cpprestsdk REQUIRED)
endif ()
option(TECKOS_USE_EXTERNAL_JSON "Use an external JSON library" OFF)
if (TECKOS_USE_EXTERNAL_JSON)
    find_package(nlohmann_json 3.2.0 REQUIRED)
else ()
    set(JSON_BuildTests OFF CACHE INTERNAL "")
    set(JSON_Install ON CACHE INTERNAL "")
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/nlohmann_json EXCLUDE_FROM_ALL)
endif ()


#################################################
#
#   Library artifact
#
#################################################
add_library(${PROJECT_NAME} ${PROJECT_SOURCE_DIR}/src/teckos/client.cc)
add_library(${PROJECT_NAME}Static STATIC ${PROJECT_SOURCE_DIR}/src/teckos/client.cc)
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})
add_library(${PROJECT_NAME}::${PROJECT_NAME}Static ALIAS ${PROJECT_NAME}Static)
target_include_directories(${PROJECT_NAME} PUBLIC
        $<BUILD_INTERFACE:${INCLUDE_DIR}>
        $<INSTALL_INTERFACE:include>)
target_include_directories(${PROJECT_NAME}Static PUBLIC
        $<BUILD_INTERFACE:${INCLUDE_DIR}>
        $<INSTALL_INTERFACE:include>)
if (CMAKE_COMPILER_IS_GNUCC)
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -pedantic -Werror)
    target_compile_options(${PROJECT_NAME}Static PRIVATE -Wall -Wextra -pedantic -Werror)
endif ()

if (USE_IX_WEBSOCKET)
    target_compile_definitions(${PROJECT_NAME}
            PRIVATE
            USE_IX_WEBSOCKET)
    target_link_libraries(${PROJECT_NAME}
            PUBLIC
            nlohmann_json::nlohmann_json
            IXWebSocket)
    target_link_libraries(${PROJECT_NAME}Static
            PUBLIC
            nlohmann_json::nlohmann_json
            IXWebSocket)
else ()
    target_link_libraries(${PROJECT_NAME}
            PUBLIC
            nlohmann_json::nlohmann_json
            cpprestsdk::cpprest)
    target_link_libraries(${PROJECT_NAME}Static
            PUBLIC
            nlohmann_json::nlohmann_json
            cpprestsdk::cpprest)
endif ()


#################################################
#
#   Example
#
#################################################
add_executable(${PROJECT_NAME}-example EXCLUDE_FROM_ALL "${CMAKE_CURRENT_SOURCE_DIR}/src/cli.cc")
set_target_properties(${PROJECT_NAME}-example PROPERTIES OUTPUT_NAME cli)
target_include_directories(${PROJECT_NAME}-example PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(${PROJECT_NAME}-example
        PUBLIC
        ${PROJECT_NAME})


#################################################
#
#   Installation
#
#################################################
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
        TARGETS ${PROJECT_NAME} ${PROJECT_NAME}Static
        NAMESPACE ${PROJECT_NAME}::
        FILE "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Targets.cmake")
install(
        TARGETS ${PROJECT_NAME} ${PROJECT_NAME}Static
        EXPORT ${TARGETS_EXPORT_NAME}
        INCLUDES DESTINATION ${INCLUDE_INSTALL_DIR})
install(
        EXPORT ${TARGETS_EXPORT_NAME}
        NAMESPACE ${PROJECT_NAME}::
        DESTINATION ${LIB_INSTALL_DIR}/cmake/${PROJECT_NAME})