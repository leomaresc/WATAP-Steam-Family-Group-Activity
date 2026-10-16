cmake_minimum_required(VERSION 3.20)

# Steam Recent Play - visor ultraligero de actividad reciente de una familia de Steam.
# Aplicacion nativa Win32 (C++20) sin dependencias de runtime externas.

project(SteamRecentPlay
    VERSION 1.0.0
    DESCRIPTION "Visor ultraligero de actividad reciente de una familia de Steam"
    LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Ejecutable dentro de un arbol de codigo; sin runtime adicional.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(MSVC)
    add_compile_options(
        /W4
        /permissive-
        /utf-8
        /Zc:__cplusplus
        /EHsc
        /MP
    )
    add_compile_definitions(
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )
    # Tamano y rendimiento: optimizacion por tamano en Release.
    add_link_options(/INCREMENTAL:NO)
    set(CMAKE_CXX_FLAGS_RELEASE "/O1 /Ob1 /DNDEBUG")
    add_compile_options($<$<CONFIG:Release>:/Gy>)
    add_compile_options($<$<CONFIG:Release>:/Gw>)
    add_link_options($<$<CONFIG:Release>:/OPT:REF>)
    add_link_options($<$<CONFIG:Release>:/OPT:ICF>)
endif()

# --- Biblioteca compartida con toda la logica no-UI (testeable sin ventana) ---
add_library(srp_core STATIC
    src/common/format.cpp
    src/common/paths.cpp
    src/steam/http.cpp
    src/steam/steam_api.cpp
    src/steam/demo_data.cpp
    src/storage/config.cpp
    src/storage/cache.cpp
    src/storage/credentials.cpp
)

target_include_directories(srp_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_link_libraries(srp_core PUBLIC
    winhttp
    crypt32
    ole32
    shell32
    advapi32
)

if(NOT MSVC)
    find_package(Threads REQUIRED)
    target_link_libraries(srp_core PUBLIC Threads::Threads)
endif()

# --- Aplicacion GUI ---
add_executable(SteamRecentPlay WIN32
    src/main.cpp
    src/ui/app.cpp
    src/ui/theme.cpp
    src/ui/gfx.cpp
    src/ui/image_cache.cpp
    src/ui/dialogs.cpp
    src/ui/worker.cpp
    resources/app.rc
)

target_link_libraries(SteamRecentPlay PRIVATE
    srp_core
    gdiplus
    comctl32
    comdlg32
    user32
    gdi32
    winmm
    dwmapi
    uxtheme
    shlwapi
)

set_target_properties(SteamRecentPlay PROPERTIES
    OUTPUT_NAME "SteamRecentPlay"
)

# --- Pruebas (consola, sin UI) ---
add_executable(srp_tests
    tests/test_main.cpp
)
target_link_libraries(srp_tests PRIVATE srp_core)
target_compile_definitions(srp_tests PRIVATE SRP_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")

enable_testing()
add_test(NAME srp_tests COMMAND srp_tests)

# --- Instalacion simple ---
install(TARGETS SteamRecentPlay RUNTIME DESTINATION .)
