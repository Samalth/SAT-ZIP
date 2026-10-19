#include "widgets.hpp"

#include "imgui.h"

#include <cstdio>

namespace szgui {

std::string fmt_size(uint64_t b) {
    char buf[32];
    if (b < 1024) snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)b);
    else if (b < 1024ull * 1024) snprintf(buf, sizeof(buf), "%.1f KB", b / 1024.0);
    else if (b < 1024ull * 1024 * 1024) snprintf(buf, sizeof(buf), "%.1f MB", b / 1048576.0);
    else snprintf(buf, sizeof(buf), "%.2f GB", b / 1073741824.0);
    return buf;
}


void wrap_same_line(float need) {
    ImGui::SameLine();
    if (ImGui::GetContentRegionAvail().x < need) ImGui::NewLine();
}


void right_align(float width) {
    float x = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - width;
    if (x > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(x);
}

}
