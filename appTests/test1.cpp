#include "doctest.h"

int add(int a, int b) { return a + b; }

TEST_CASE("add test") { CHECK(add(1, 2) == 3); }