cmake_minimum_required(VERSION 3.23)

# check if GitLab CI/CD
if($ENV{CI})
    set(GIT_REFSPEC $ENV{CI_COMMIT_REF_NAME})
    set(GIT_HASH $ENV{CI_COMMIT_SHA})
    set(GIT_TAG $ENV{CI_COMMIT_TAG})
    set(BUILD_TIMESTAMP $ENV{CI_COMMIT_TIMESTAMP})
else()
    string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S")
    include(cmake/GetGitRevisionDescription.cmake)
    get_git_head_revision(GIT_REFSPEC GIT_HASH)
    git_describe_working_tree(GIT_TAG --match "v*")
endif()
message(STATUS "BUILDING DLNAModule-${GIT_REFSPEC}-${GIT_HASH} ${GIT_TAG}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/DLNAConfig.h.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/DLNAConfig.h"
    @ONLY
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(ENABLE_SLOG OFF CACHE BOOL "link libslog shared library")

project(DLNAModule)

add_library(${PROJECT_NAME} SHARED)
add_subdirectory("contrib/pupnp")

target_sources(${PROJECT_NAME} 
    PRIVATE
    "src/DLNAModule.cpp" 
    "src/DLNAInterface.cpp"
    "$<$<NOT:$<BOOL:${ENABLE_SLOG}>>:src/logger.cpp>"
)

target_include_directories(${PROJECT_NAME} PRIVATE
    "src"
)

target_compile_options(${PROJECT_NAME} PUBLIC $<$<BOOL:${MSVC}>:/MP /utf-8 /Zc:__cplusplus>)
target_compile_definitions(${PROJECT_NAME} 
    PUBLIC $<IF:$<CONFIG:Debug>,DEBUG,NDEBUG>
    PUBLIC $<$<BOOL:${MSVC}>:_WINDLL _UNICODE UNICODE _CONSOLE>
    PUBLIC $<$<BOOL:${ENABLE_SLOG}>:ENABLE_SLOG>
)
target_link_directories(${PROJECT_NAME} PRIVATE $<$<BOOL:${ENABLE_SLOG}>:${CMAKE_CURRENT_SOURCE_DIR}/contrib/slog>)
target_link_libraries(${PROJECT_NAME} PRIVATE UPNP::Static IXML::Static $<$<BOOL:${ENABLE_SLOG}>:slog>)
if(${CMAKE_SYSTEM_NAME} MATCHES "Android")
    find_library(log-lib log)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${log-lib})
endif()
target_link_options(${PROJECT_NAME} PUBLIC $<$<BOOL:${MSVC}>:/DEF:"${CMAKE_CURRENT_SOURCE_DIR}/src/DLNAModule.def">)

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
