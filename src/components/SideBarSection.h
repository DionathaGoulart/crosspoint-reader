#pragma once

#include <cstdint>

// Which root screen a theme's side bar highlights. The order is the order of the bar's cells.
enum class SideBarSection : int8_t { None = -1, Home, Books, Files, Send, Setup, Count };
