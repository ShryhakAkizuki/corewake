cmake_minimum_required(VERSION 3.28)
project(corewake LANGUAGES C CXX)

# Estándar C++ mínimo
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Opciones de compilación
option(ENABLE_TESTS "Habilitar pruebas unitarias" ON)

# ------------------------------------------------------------------
# Core
# ------------------------------------------------------------------
add_library(corewake_core STATIC
    src/core/wol_packet.c
    src/core/aliases_cache.c
    src/core/translator.c
)
target_include_directories(corewake_core PUBLIC src)

# ------------------------------------------------------------------
# Raspberry Pi 5 (Linux) 
# ------------------------------------------------------------------
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(httplib CONFIG REQUIRED)

    add_library(corewake_adapter_pi STATIC
        src/adapters/raspberry_pi/sender_linux.cpp
        src/adapters/raspberry_pi/loader_linux.cpp
        src/adapters/raspberry_pi/listener_linux.cpp
    )
    target_link_libraries(corewake_adapter_pi PUBLIC corewake_core httplib::httplib)

    add_subdirectory(platform/raspberry_pi)
endif()

# ------------------------------------------------------------------
# Pruebas
# ------------------------------------------------------------------
if(ENABLE_TESTS)
    enable_testing()
    find_package(Catch2 CONFIG REQUIRED)
    add_subdirectory(tests)
endif()
