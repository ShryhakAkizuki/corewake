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

    # Core
    add_executable(test_core tests/test_core.cpp)
    target_link_libraries(test_core PRIVATE corewake_core Catch2::Catch2WithMain)
    add_test(NAME test_core COMMAND test_core)

    # Raspberry Pi 5 (Linux) 
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")

        # Sender Adapter
        add_executable(test_raspberry_pi_sender_adapter tests/test_raspberry_pi_sender_adapter.cpp)
        target_link_libraries(test_raspberry_pi_sender_adapter PRIVATE
            corewake_adapter_pi corewake_core Catch2::Catch2WithMain)
        add_test(NAME test_raspberry_pi_sender_adapter COMMAND test_raspberry_pi_sender_adapter)

        # Loader Adapter
        add_executable(test_raspberry_pi_loader_adapter tests/test_raspberry_pi_loader_adapter.cpp)
        target_link_libraries(test_raspberry_pi_loader_adapter PRIVATE
            corewake_adapter_pi corewake_core Catch2::Catch2WithMain)
        add_test(NAME test_raspberry_pi_loader_adapter COMMAND test_raspberry_pi_loader_adapter)

        # Listener Adapter
        add_executable(test_raspberry_pi_listener_adapter tests/test_raspberry_pi_listener_adapter.cpp)
        target_link_libraries(test_raspberry_pi_listener_adapter PRIVATE
            corewake_adapter_pi corewake_core Catch2::Catch2WithMain)
        add_test(NAME test_raspberry_pi_listener_adapter COMMAND test_raspberry_pi_listener_adapter)

        # Integration - composition root de platform/raspberry_pi/main.cpp
        add_executable(test_raspberry_pi_integration tests/test_raspberry_pi_integration.cpp)
        target_link_libraries(test_raspberry_pi_integration PRIVATE
            corewake_adapter_pi corewake_core Catch2::Catch2WithMain)
        add_test(NAME test_raspberry_pi_integration COMMAND test_raspberry_pi_integration)

        # Test Aliases.INI
        add_custom_command(TARGET test_raspberry_pi_loader_adapter POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                -DENV=${CMAKE_SOURCE_DIR}/tests/fixtures/aliases.env
                -DGENERIC=${CMAKE_SOURCE_DIR}/aliases/aliases.INI
                -DOUT=${CMAKE_BINARY_DIR}/aliases/aliases.INI
                -P ${CMAKE_SOURCE_DIR}/cmake/generate_aliases.cmake
            COMMENT "corewake: generando fixture aliases.INI para test_loader_linux (build-time)")
    endif()

endif()

# Force CPP version on windows
if(MSVC AND ENABLE_TESTS)
    target_compile_options(test_core PRIVATE /Zc:__cplusplus)
endif()
