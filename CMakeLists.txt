cmake_minimum_required(VERSION 3.16.0)

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
set(TARGET_NAME ${PROJECT_NAME})
set(TARGETS_EXPORT_NAME "${PROJECT_NAME}Targets")
set(INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)
set(INCLUDE_INSTALL_DIR ${CMAKE_INSTALL_INCLUDEDIR} CACHE PATH "Install path for include files")
set(LIB_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR} CACHE PATH "Install path for library files")


#################################################
#
#   Environment
#
#################################################
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
option(USE_IX_WEBSOCKET "Use ixwebsocket instead of cpprestsdk" OFF)

option(DEBUG_TECKOS_SEND "Print messages to console when sending" OFF)
option(DEBUG_TECKOS_RECV "Print messages to console when receiving" OFF)


#################################################
#
#   Dependencies
#
#################################################
if (USE_IX_WEBSOCKET)
    # Want to remove the following, but Windows seems to need this - at least @github-actions:
    #if (${CMAKE_PROJECT_NAME} STREQUAL ${PROJECT_NAME})
    if (WIN32)
        include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake OPTIONAL)
        if (COMMAND conan_basic_setup)
            conan_basic_setup()
        endif ()
    endif ()
    #endif ()

    set(USE_TLS ON CACHE BOOL "Support TLS" FORCE)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/IXWebSocket EXCLUDE_FROM_ALL)
else ()
    find_package(cpprestsdk REQUIRED)
    find_package(OpenSSL REQUIRED)
endif ()
find_package(nlohmann_json 3.2.0 REQUIRED)
find_package(spdlog REQUIRED)


#################################################
#
#   Library artifact
#
#################################################
set(SOURCES 
    include/teckos/global.h
    src/teckos/client.cc include/teckos/client.h
    src/teckos/rest.cc include/teckos/rest.h)


add_library(${PROJECT_NAME} ${SOURCES} )
add_library(${PROJECT_NAME}Static STATIC ${SOURCES})
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
if (APPLE)
    if (CMAKE_OSX_DEPLOYMENT_TARGET LESS_EQUAL "10.14")
        # See: https://github.com/nlohmann/json/issues/3156
        target_compile_definitions(${PROJECT_NAME} PUBLIC JSON_HAS_CPP_14)
        target_compile_definitions(${PROJECT_NAME}Static PUBLIC JSON_HAS_CPP_14)
    endif ()
endif ()

target_link_libraries(${PROJECT_NAME}
        PUBLIC
        nlohmann_json::nlohmann_json spdlog::spdlog)
target_link_libraries(${PROJECT_NAME}Static
        PUBLIC
        nlohmann_json::nlohmann_json spdlog::spdlog)

if (USE_IX_WEBSOCKET)
    target_compile_definitions(${PROJECT_NAME}
            PUBLIC
            USE_IX_WEBSOCKET)
    target_compile_definitions(${PROJECT_NAME}Static
            PUBLIC
            USE_IX_WEBSOCKET)
    target_link_libraries(${PROJECT_NAME}
            PUBLIC
            ixwebsocket)
    target_link_libraries(${PROJECT_NAME}Static
            PUBLIC
            ixwebsocket)
else ()
    target_link_libraries(${PROJECT_NAME}
            PUBLIC
            cpprestsdk::cpprest
            OpenSSL::Crypto)
    target_link_libraries(${PROJECT_NAME}Static
            PUBLIC
            cpprestsdk::cpprest
            OpenSSL::Crypto)
endif ()
if (DEBUG_TECKOS_SEND)
    target_compile_definitions(${PROJECT_NAME}
            PUBLIC
            DEBUG_TECKOS_SEND)
    target_compile_definitions(${PROJECT_NAME}Static
            PUBLIC
            DEBUG_TECKOS_SEND)
endif (DEBUG_TECKOS_SEND)
if (DEBUG_TECKOS_RECV)
    target_compile_definitions(${PROJECT_NAME}
            PUBLIC
            DEBUG_TECKOS_RECV)
    target_compile_definitions(${PROJECT_NAME}Static
            PUBLIC
            DEBUG_TECKOS_RECV)
endif (DEBUG_TECKOS_RECV)

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
if (USE_IX_WEBSOCKET)
    export(
            TARGETS ${PROJECT_NAME} ${PROJECT_NAME}Static ixwebsocket
            NAMESPACE ${PROJECT_NAME}::
            FILE "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Targets.cmake")
else ()
    export(
            TARGETS ${PROJECT_NAME} ${PROJECT_NAME}Static
            NAMESPACE ${PROJECT_NAME}::
            FILE "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Targets.cmake")
endif ()
install(
        TARGETS ${PROJECT_NAME} ${PROJECT_NAME}Static
        EXPORT ${TARGETS_EXPORT_NAME}
        INCLUDES DESTINATION ${INCLUDE_INSTALL_DIR})

install(
        EXPORT ${TARGETS_EXPORT_NAME}
        NAMESPACE ${PROJECT_NAME}::
        DESTINATION ${LIB_INSTALL_DIR}/cmake/${PROJECT_NAME})