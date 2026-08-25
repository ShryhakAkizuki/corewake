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
    add_library(corewake_adapter_pi STATIC
        src/adapters/raspberry_pi/sender_linux.cpp
    )
    target_link_libraries(corewake_adapter_pi PUBLIC corewake_core)

    add_subdirectory(platform/raspberry_pi)
endif()

# ------------------------------------------------------------------
# Pruebas
# ------------------------------------------------------------------
if(ENABLE_TESTS)
    enable_testing()
    find_package(Catch2 CONFIG REQUIRED)

    # Core
    add_executable(test_core tests/test_core.cpp)
    target_link_libraries(test_core PRIVATE corewake_core Catch2::Catch2WithMain)
    add_test(NAME test_core COMMAND test_core)

    # Raspberry Pi 5 (Linux) 
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(test_adapter_raspberry_pi tests/test_adapter_raspberry_pi.cpp)
        target_link_libraries(test_adapter_raspberry_pi PRIVATE
            corewake_adapter_pi corewake_core Catch2::Catch2WithMain)
        add_test(NAME test_adapter_raspberry_pi COMMAND test_adapter_raspberry_pi)
    endif()

endif()

# Force CPP version on windows
if(MSVC AND ENABLE_TESTS)
    target_compile_options(test_core PRIVATE /Zc:__cplusplus)
endif()
