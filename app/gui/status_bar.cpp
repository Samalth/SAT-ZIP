#include "status_bar.hpp"
#include "fonts.hpp"

#include "imgui.h"

#include <cstdio>

namespace szgui {

void draw_footer(App* a) {
    std::string status;
    { std::lock_guard<std::mutex> lk(a->mtx); status = a->status; }
    want_glyphs(status);
    if (a->running) {
        size_t d = a->prog_done, t = a->prog_total;
        float f = t ? (float)d / (float)t : 0.0f;
        if (ImGui::Button("Cancel")) a->cancel = true;
        ImGui::SetItemTooltip("Stop the current operation");
        ImGui::SameLine();
        char pct[16];
        snprintf(pct, sizeof(pct), "%d%%", (int)(f * 100.0f + 0.5f));
        ImGui::ProgressBar(f, ImVec2(-1, ImGui::GetFrameHeight()), pct);
        ImGui::TextDisabled("%s", status.c_str());
    } else {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", status.c_str());
    }
}

float footer_height(const App* a) {
    const ImGuiStyle& st = ImGui::GetStyle();
    float rows = a->running ? ImGui::GetFrameHeight() + st.ItemSpacing.y + ImGui::GetTextLineHeight()
                            : ImGui::GetFrameHeight();
    return rows + st.ItemSpacing.y + 1.0f;
}

}
