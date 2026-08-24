cmake_minimum_required(VERSION 3.28)
project(corewake LANGUAGES C CXX)

# Estándar C++ mínimo
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Opciones de compilación
option(ENABLE_TESTS "Habilitar pruebas unitarias" ON)
option(ENABLE_CUDA "Habilitar soporte CUDA" OFF)

# Fuente principal
add_executable(${PROJECT_NAME}
    src/main.cpp
    src/core/wol_packet.c
)
target_include_directories(${PROJECT_NAME} PRIVATE src)

# Dependencias mediante vcpkg
# find_package(fmt CONFIG REQUIRED)
# target_link_libraries(${PROJECT_NAME} PRIVATE fmt::fmt)

# Pruebas
if(ENABLE_TESTS)
    enable_testing()
    find_package(Catch2 CONFIG REQUIRED)

    #  Core
    add_executable(test_core tests/test_core.cpp src/core/wol_packet.c src/core/aliases_cache.c)
    target_include_directories(test_core PRIVATE src)
    target_link_libraries(test_core PRIVATE Catch2::Catch2WithMain)
    add_test(NAME test_core COMMAND test_core)

    # Adapters
    # add_executable(test_adapters tests/test_adapters.cpp src/core/wol_packet.c)
    # target_include_directories(test_adapters PRIVATE src)
    # target_link_libraries(test_adapters PRIVATE Catch2::Catch2WithMain)
    # add_test(NAME test_adapters COMMAND test_adapters)
endif()

# CUDA (Futuro)
if(ENABLE_CUDA)
    enable_language(CUDA)
    # Configuración CUDA pendiente
endif()

# Forzar version de CPP en windows
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /Zc:__cplusplus)
endif()