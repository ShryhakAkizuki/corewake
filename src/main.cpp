#include <iostream>

int main() {
    // Compilador y versión
    std::cout << "Compilador: ";
#if defined(__clang__)
    std::cout << "Clang " << __clang_major__ << "." << __clang_minor__ << "." << __clang_patchlevel__;
#elif defined(_MSC_VER)
    std::cout << "MSVC " << _MSC_VER;
#elif defined(__GNUC__)
    std::cout << "GCC " << __GNUC__ << "." << __GNUC_MINOR__ << "." << __GNUC_PATCHLEVEL__;
#else
    std::cout << "Desconocido";
#endif
    std::cout << std::endl;

    std::cout << "Estándar C++: " << __cplusplus << std::endl;

    std::cout << "Plataforma: ";
#if defined(_WIN32)
    std::cout << "Windows";
#elif defined(__linux__)
    std::cout << "Linux";
#else
    std::cout << "Desconocida";
#endif

    std::cout << " (";
#if defined(__x86_64__) || defined(_M_X64)
    std::cout << "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    std::cout << "ARM64 (AArch64)";
#else
    std::cout << "Desconocida";
#endif
    std::cout << ")" << std::endl;

    // Verificar C++17
#if __cplusplus >= 201703L
    std::cout << "C++17 o superior: OK" << std::endl;
#else
    std::cout << "C++17 o superior: NO" << std::endl;
#endif

    return 0;
}