cmake_minimum_required(VERSION 3.20)
project(LegacyMenuBridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)

set(CORE_SOURCES
    src/Log.cpp
    src/LegacyEnumerator.cpp
    src/EnumExplorerCommand.cpp
    src/ExplorerCommand.cpp
)

# --- COM server DLL (the modern-menu command provider) ---
add_library(LegacyMenuBridge SHARED
    src/dllmain.cpp
    ${CORE_SOURCES}
    src/exports.def
)
target_include_directories(LegacyMenuBridge PRIVATE src)
target_link_libraries(LegacyMenuBridge PRIVATE shell32 ole32 oleaut32 shlwapi user32 uuid)
set_target_properties(LegacyMenuBridge PROPERTIES
    PREFIX "" SUFFIX ".dll" OUTPUT_NAME "LegacyMenuBridge")

# --- Small host EXE, required by the sparse package manifest ---
add_executable(LegacyMenuBridgeHost WIN32 src/main.cpp)
set_target_properties(LegacyMenuBridgeHost PROPERTIES
    PREFIX "" SUFFIX ".exe" OUTPUT_NAME "LegacyMenuBridge")

if(MSVC)
    target_compile_options(LegacyMenuBridge PRIVATE /W4 /utf-8)
    target_compile_options(LegacyMenuBridgeHost PRIVATE /W4 /utf-8)
endif()

# Drop the manifest next to the binaries so the staging scripts can find it.
add_custom_command(TARGET LegacyMenuBridge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/packaging/AppxManifest.xml"
            "$<TARGET_FILE_DIR:LegacyMenuBridge>/AppxManifest.xml"
    VERBATIM)
