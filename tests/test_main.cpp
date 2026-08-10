#include <catch2/catch_test_macros.hpp>

// --------------------------------------------------------------
// TEST_CASE: agrupa un escenario de prueba bajo un nombre legible
// y tags entre corchetes para filtrar (ej. correr solo "[auth]").
// --------------------------------------------------------------
TEST_CASE("Descripción del comportamiento a probar", "[categoria]") {

    // SECTION: subcasos dentro del mismo TEST_CASE. Catch2 corre
    // cada SECTION de forma aislada, reiniciando el estado previo
    // a cada una — útil para setup/teardown compartido sin fixtures.
    SECTION("caso feliz") {
        REQUIRE(true);   // detiene el test si falla
    }

    SECTION("caso límite") {
        CHECK(true);     // reporta el fallo pero sigue ejecutando el resto
    }
}

// Puedes tener varios TEST_CASE por archivo, y varios archivos
// .cpp de test enlazados al mismo target 'tests' en CMakeLists.txt.
TEST_CASE("Otro escenario independiente", "[categoria]") {
    REQUIRE(1 + 1 == 2);
}