#pragma once

// FAKE HARDWARE HEADER FOR HOST TESTS

// This file is created only to prevent the host compiler from crashing
// when reading hardware header inclusions. It is not deployed to the real board.

#include <cstdint>

// If your firmware files reference hardware handles or variables, we define them
// here as empty dummy structs so the compiler recognizes the types without errors:
struct UART_HandleTypeDef {};
struct IWDG_HandleTypeDef {};
