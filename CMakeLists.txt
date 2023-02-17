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

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(ENABLE_SLOG OFF CACHE BOOL "link libslog shared library")

project(DLNAModule)
include(CTest)
if(BUILD_TESTING)
    #add_subdirectory("test")
endif()

add_subdirectory("src")
add_subdirectory("contrib/pupnp")
add_subdirectory("contrib/rapidjson")

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
