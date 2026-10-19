#pragma once

#include "imgui.h"

namespace szgui {

ImVec4 rgb(unsigned v, float a = 1.0f);
void apply_style(bool dark);
void push_primary_button();

}
