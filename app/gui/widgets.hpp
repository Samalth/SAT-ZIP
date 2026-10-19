#pragma once

#include <cstdint>
#include <string>

namespace szgui {

std::string fmt_size(uint64_t b);
void wrap_same_line(float need);
void right_align(float width);

}
