cmake_minimum_required(VERSION 3.28)
project(corewake LANGUAGES CXX)

# Estándar C++ mínimo
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Opciones de compilación
option(ENABLE_TESTS "Habilitar pruebas unitarias" ON)
option(ENABLE_CUDA "Habilitar soporte CUDA" OFF)

# Fuente principal
add_executable(${PROJECT_NAME} src/main.cpp)

# Dependencias mediante vcpkg
# find_package(fmt CONFIG REQUIRED)
# target_link_libraries(${PROJECT_NAME} PRIVATE fmt::fmt)

# Pruebas
if(ENABLE_TESTS)
    enable_testing()
    find_package(Catch2 CONFIG REQUIRED)
    add_executable(tests tests/test_main.cpp)
    target_link_libraries(tests PRIVATE Catch2::Catch2WithMain)
    add_test(NAME ${PROJECT_NAME}_tests COMMAND tests)
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