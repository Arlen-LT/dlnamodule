configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/DLNAConfig.h.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/DLNAConfig.h"
    @ONLY
)

add_library(${PROJECT_NAME} SHARED)

target_sources(${PROJECT_NAME} 
    PRIVATE
    "base64.cpp"
    "UpnpCommand.cpp"
    "URLHandler.cpp"
    "DLNAModule.cpp" 
    "DLNAInterface.cpp"
    "$<$<NOT:$<BOOL:${ENABLE_SLOG}>>:logger.cpp>"
)

target_include_directories(${PROJECT_NAME} PRIVATE
    "src"

    "${CMAKE_SOURCE_DIR}/contrib/slog/include"
    "${CMAKE_SOURCE_DIR}/contrib/rapidjson/include"
)

target_compile_options(${PROJECT_NAME} PUBLIC $<$<BOOL:${MSVC}>:/MP /utf-8 /Zc:__cplusplus>)
target_compile_definitions(${PROJECT_NAME} 
    PUBLIC $<IF:$<CONFIG:Debug>,DEBUG,NDEBUG>
    PUBLIC $<$<BOOL:${MSVC}>:_WINDLL _UNICODE UNICODE _CONSOLE>
    PUBLIC $<$<BOOL:${ENABLE_SLOG}>:ENABLE_SLOG>
)
target_link_libraries(${PROJECT_NAME} PRIVATE UPNP::Static IXML::Static $<$<BOOL:${ENABLE_SLOG}>:${CMAKE_SOURCE_DIR}/contrib/slog/lib/libslog_static.a> $<$<BOOL:${ANDROID}>:log>)
target_link_options(${PROJECT_NAME} PUBLIC $<$<BOOL:${MSVC}>:/DEF:"${CMAKE_CURRENT_SOURCE_DIR}/DLNAModule.def">)