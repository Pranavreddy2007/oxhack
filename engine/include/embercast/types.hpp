#pragma once
// Grid, Params, Frame, Plan. Fixed sizes. Step 2 fills this file.

#include <cstdint>

namespace embercast {

const char* version();
const char* safety_notice();

enum class CellState : std::uint8_t { Unburned = 0, Burning = 1, Burnt = 2 };

}  // namespace embercast
