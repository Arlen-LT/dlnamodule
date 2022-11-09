cmake_minimum_required(VERSION 3.23)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

#include(GetGitRevisionDescription)
#get_git_head_revision(GIT_REFSPEC GIT_HASH)

project(DLNAModule VERSION 0.1)

#set(CMAKE_CXX_EXTENSIONS OFF)
#set(CLANG_DEFAULT_CXX_STDLIB "libc++")

add_library(${PROJECT_NAME} SHARED)
add_subdirectory("contrib/pupnp")

target_sources(${PROJECT_NAME} 
    PRIVATE
    "src/DLNAModule.cpp"

    INTERFACE 
    "src/DLNAInterface.cpp"
)

target_include_directories(${PROJECT_NAME} PRIVATE
    "src"
)
target_link_libraries(${PROJECT_NAME} UPNP::Static IXML::Static)

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
