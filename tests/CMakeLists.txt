
# Includes de helpers (util y fixtures)
set(COREWAKE_TEST_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR})

# ================================================================
# Unit - core (cross-platform)
# ================================================================
foreach(t test_wol_packet test_aliases_cache test_translator)
    add_executable(${t} unit/core/${t}.cpp)
    target_include_directories(${t} PRIVATE ${COREWAKE_TEST_INCLUDE_DIRS})
    target_link_libraries(${t} PRIVATE corewake_core Catch2::Catch2WithMain)
    add_test(NAME ${t} COMMAND ${t})
    set_tests_properties(${t} PROPERTIES LABELS "core;unit")
endforeach()

# Catch2 en MSVC requiere /Zc:__cplusplus
if(MSVC)
    foreach(t test_wol_packet test_aliases_cache test_translator)
        target_compile_options(${t} PRIVATE /Zc:__cplusplus)
    endforeach()
endif()

# ================================================================
# Raspberry Pi 5
# ================================================================
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(COREWAKE_BUILD_ALIASES ${CMAKE_CURRENT_BINARY_DIR}/aliases/aliases.INI)

    # -------------------- Unit - adaptadores --------------------
    foreach(t test_loader_linux test_sender_linux test_listener_linux)
        add_executable(${t} unit/adapters/raspberry_pi/${t}.cpp)
        target_include_directories(${t} PRIVATE ${COREWAKE_TEST_INCLUDE_DIRS})
        target_link_libraries(${t} PRIVATE corewake_adapter_pi Catch2::Catch2WithMain)
        add_test(NAME ${t} COMMAND ${t})
        set_tests_properties(${t} PROPERTIES LABELS "pi;unit")
    endforeach()

    target_compile_definitions(test_loader_linux PRIVATE
        COREWAKE_DEFAULT_INI="${COREWAKE_BUILD_ALIASES}")

    add_custom_command(TARGET test_loader_linux POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DENV=${CMAKE_SOURCE_DIR}/tests/fixtures/aliases.env
            -DGENERIC=${CMAKE_SOURCE_DIR}/aliases/aliases.INI
            -DOUT=${COREWAKE_BUILD_ALIASES}
            -P ${CMAKE_SOURCE_DIR}/cmake/generate_aliases.cmake
        COMMENT "corewake: generando fixture aliases.INI para test_loader_linux (build-time)")

    # -------------------- Integración --------------------
    foreach(t test_loader_to_cache test_core_to_sender)
        add_executable(${t} integration/raspberry_pi/${t}.cpp)
        target_include_directories(${t} PRIVATE ${COREWAKE_TEST_INCLUDE_DIRS})
        target_link_libraries(${t} PRIVATE corewake_adapter_pi Catch2::Catch2WithMain)
        add_test(NAME ${t} COMMAND ${t})
        set_tests_properties(${t} PROPERTIES LABELS "pi;integration")
    endforeach()

    # -------------------- E2E --------------------
    add_executable(test_http_wake_flow e2e/raspberry_pi/test_http_wake_flow.cpp)
    target_include_directories(test_http_wake_flow PRIVATE ${COREWAKE_TEST_INCLUDE_DIRS})
    target_link_libraries(test_http_wake_flow PRIVATE corewake_adapter_pi Catch2::Catch2WithMain)
    add_test(NAME test_http_wake_flow COMMAND test_http_wake_flow)
    set_tests_properties(test_http_wake_flow PROPERTIES LABELS "pi;e2e")

endif()
