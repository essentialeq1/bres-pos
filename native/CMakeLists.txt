cmake_minimum_required(VERSION 3.20)
project(LegacyMenuBridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)

# --- Embed the app icon (root "Classic Menu" icon + fallback for items) ---
# The .rc is generated with an absolute icon path so rc.exe resolves it no
# matter what its working directory is.
set(APP_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/app.ico")
set(LMB_RESOURCES)
if(EXISTS "${APP_ICON_PATH}")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/LegacyMenuBridge.rc.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/LegacyMenuBridge.rc" @ONLY)
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/LegacyMenuBridge.rc"
                                PROPERTIES GENERATED TRUE)
    list(APPEND LMB_RESOURCES "${CMAKE_CURRENT_BINARY_DIR}/LegacyMenuBridge.rc")
else()
    message(WARNING "src/app.ico not found; building without an embedded icon")
endif()

# --- Shim DLL: in-proc IExplorerCommand; never loads legacy handler DLLs itself ---
add_library(LegacyMenuBridge SHARED
    src/dllmain.cpp
    src/EnumExplorerCommand.cpp
    src/ExplorerCommand.cpp
    src/Snapshot.cpp
    src/Log.cpp
    src/exports.def
    ${LMB_RESOURCES}
)
target_include_directories(LegacyMenuBridge PRIVATE src)
target_link_libraries(LegacyMenuBridge PRIVATE shell32 ole32 oleaut32 shlwapi user32 uuid)
set_target_properties(LegacyMenuBridge PROPERTIES PREFIX "" SUFFIX ".dll" OUTPUT_NAME "LegacyMenuBridge")

# --- Worker EXE: hosts the (potentially crashing) legacy menu construction/invoke ---
add_executable(LegacyMenuBridgeHost
    src/main.cpp
    src/LegacyEnumerator.cpp
    src/Log.cpp
)
target_include_directories(LegacyMenuBridgeHost PRIVATE src)
target_link_libraries(LegacyMenuBridgeHost PRIVATE shell32 ole32 shlwapi advapi32 user32 uuid)
set_target_properties(LegacyMenuBridgeHost PROPERTIES PREFIX "" SUFFIX ".exe" OUTPUT_NAME "LegacyMenuBridge")

if(MSVC)
    target_compile_options(LegacyMenuBridge PRIVATE /W4 /utf-8)
    target_compile_options(LegacyMenuBridgeHost PRIVATE /W4 /utf-8)
endif()

add_custom_command(TARGET LegacyMenuBridge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/packaging/AppxManifest.xml"
            "$<TARGET_FILE_DIR:LegacyMenuBridge>/AppxManifest.xml"
    VERBATIM)
