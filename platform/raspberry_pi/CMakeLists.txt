# ------------------------------------------------------------------
# Raspberry Pi 5 (Linux) — composition root
# ------------------------------------------------------------------

add_executable(corewake_pi main.cpp)
target_link_libraries(corewake_pi PRIVATE corewake_adapter_pi corewake_core)

# ------------------------------------------------------------------
# Build-time generation of aliases.INI:
# ------------------------------------------------------------------
add_custom_command(TARGET corewake_pi POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DENV=${CMAKE_SOURCE_DIR}/.env
        -DGENERIC=${CMAKE_SOURCE_DIR}/aliases/aliases.INI
        -DOUT=${CMAKE_CURRENT_BINARY_DIR}/aliases/aliases.INI
        -P ${CMAKE_SOURCE_DIR}/cmake/generate_aliases.cmake
    COMMENT "corewake: generando aliases.INI (build-time)")
