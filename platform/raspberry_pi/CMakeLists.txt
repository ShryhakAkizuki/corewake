# ------------------------------------------------------------------
# Raspberry Pi 5 (Linux) — composition root
# ------------------------------------------------------------------

add_executable(corewake_pi main.cpp)
target_link_libraries(corewake_pi PRIVATE corewake_adapter_pi corewake_core)
