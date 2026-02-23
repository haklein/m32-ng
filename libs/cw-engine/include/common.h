#pragma once

#include <cstdio>

// Inject wall-clock milliseconds as a function pointer so that this library
// compiles and tests on native targets (no Arduino millis() dependency).
typedef unsigned long (*millis_fun_ptr)();
