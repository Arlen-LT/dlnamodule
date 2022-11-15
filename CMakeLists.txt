cmake_minimum_required(VERSION 3.23)

string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S")
include(cmake/GetGitRevisionDescription.cmake)
get_git_head_revision(GIT_REFSPEC GIT_HASH)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

project(DLNAModule VERSION 0.1.0)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/DLNAConfig.h.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/DLNAConfig.h"
    @ONLY
)

if(MSVC)
    add_definitions(-D_WINDLL -D_UNICODE -DUNICODE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8 /Zc:__cplusplus")
endif()
#set(CMAKE_CXX_EXTENSIONS OFF)
#set(CLANG_DEFAULT_CXX_STDLIB "libc++")

add_library(${PROJECT_NAME} SHARED)
add_subdirectory("contrib/pupnp")

target_sources(${PROJECT_NAME} 
    PRIVATE
    "src/DLNAConfig.h"
    "src/DLNAModule.h"
    "src/DLNAModule.cpp" 
    "src/DLNAInterface.cpp"
    "src/logger.h" 
    "src/logger.cpp")

target_include_directories(${PROJECT_NAME} PRIVATE
    "src"
)

set(ENABLE_SLOG OFF CACHE BOOL "link libslog shared library")
if(${ENABLE_SLOG})
    ADD_DEFINITIONS(-DENABLE_SLOG)
endif()
target_link_directories(${PROJECT_NAME} PRIVATE $<$<BOOL:${ENABLE_SLOG}>:${CMAKE_CURRENT_SOURCE_DIR}/contrib/slog>)
target_link_libraries(${PROJECT_NAME} UPNP::Static IXML::Static $<$<BOOL:${ENABLE_SLOG}>:slog>)

install (TARGETS DLNAModule
    EXPORT DLNAModule
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        COMPONENT DLNA_Development
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/DLNA
        COMPONENT DLNA_Development
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        COMPONENT DLNA_RunTime
        NAMELINK_COMPONENT DLNA_Development
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/DLNA
        COMPONENT DLNA_Development
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT DLNA_RunTime
)
