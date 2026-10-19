#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  #include <windows.h>
#endif

#include "tabs.hpp"
#include "theme.hpp"

#include "imgui.h"
#include <GLFW/glfw3.h>

namespace szgui {

bool settings_dirty(const App* a) {
    const sz::Settings& x = a->settings;
    const sz::Settings& y = a->settings_saved;
    return x.level != y.level || x.threads != y.threads || x.compress_all != y.compress_all ||
           x.mode != y.mode || x.dark != y.dark || x.zipnow_folder != y.zipnow_folder ||
           x.format != y.format || x.win_w != y.win_w || x.win_h != y.win_h;
}

void save_settings_now(App* a) {
    bool ok = sz::save_settings(a->settings);
    a->settings_saved = a->settings;
    std::lock_guard<std::mutex> lk(a->mtx);
    a->status = ok ? ("Settings saved to " + sz::settings_path().string()) : "Could not save settings";
}

void draw_settings_tab(App* a) {
    ImGui::Dummy(ImVec2(0, 8));
    if (ImGui::CollapsingHeader("Compression")) {
    const char* levels[] = {"1 (fastest)","2","3","4","5","6","7","8","9 (smallest)"};
    int li = a->settings.level - 1; if (li < 0) li = 0; if (li > 8) li = 8;
    ImGui::SetNextItemWidth(180);
    if (ImGui::Combo("default level", &li, levels, 9)) a->settings.level = li + 1;
    ImGui::SetNextItemWidth(180);
    ImGui::SliderInt("default threads", &a->settings.threads, 0, 32, a->settings.threads == 0 ? "auto" : "%d");
    ImGui::Checkbox("compress already-compressed files by default", &a->settings.compress_all);
    const char* cmodes[] = {"Fast (Mode S)", "Better ratio (Mode R)", "Max ratio (Mode T)"};
    int mi = a->settings.mode; if (mi < 0) mi = 0; if (mi > 2) mi = 2;
    ImGui::SetNextItemWidth(220);
    if (ImGui::Combo("mode", &mi, cmodes, 3)) a->settings.mode = mi;
    ImGui::SameLine(); ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Mode S  - fastest compression, good ratio.\n"
                          "Mode R  - denser search, better ratio, slower to compress\n"
                          "          (extraction stays just as fast).\n"
                          "Mode T  - Mode R + ANS entropy coding, best ratio,\n"
                          "          slowest to compress and slower to extract.");
    }

    if (ImGui::CollapsingHeader("Archive format & Zip now")) {
    const char* fmts[] = {"SAT-ZIP (.satz)", "Zip (.zip)", "Tar (.tar)", "7-Zip (.7z)", "Tar+XZ (.tar.xz)"};
    int fi = a->settings.format; if (fi < 0) fi = 0; if (fi > 4) fi = 4;
    ImGui::SetNextItemWidth(180);
    if (ImGui::Combo("default archive format", &fi, fmts, 5)) a->settings.format = fi;
    ImGui::SameLine(); ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Used by 'Zip now' and preselected in the Create Archive dialog.\n"
                          ".satz is fastest (our codec); .zip opens everywhere and is built\n"
                          "multithreaded, so it is still faster than Windows' built-in zipping.");
    ImGui::Checkbox("Zip now: wrap single files in a folder (named without extension)", &a->settings.zipnow_folder);
    ImGui::SetItemTooltip("report.pdf becomes report.satz containing report/report.pdf");
    }

    if (ImGui::CollapsingHeader("Appearance")) {
    if (ImGui::Checkbox("Dark theme", &a->settings.dark)) apply_style(a->settings.dark);
    }

    if (ImGui::CollapsingHeader("Window")) {
    ImGui::SetNextItemWidth(180); ImGui::SliderInt("default width",  &a->settings.win_w, 700, 2560);
    ImGui::SetNextItemWidth(180); ImGui::SliderInt("default height", &a->settings.win_h, 500, 1440);
    ImGui::SameLine();
    if (ImGui::Button("Apply now", ImVec2(110, 0))) glfwSetWindowSize(a->win, a->settings.win_w, a->settings.win_h);
    ImGui::SetItemTooltip("Resize the window right away");
    ImGui::TextDisabled("The UI scales with the window height.");
    }

    ImGui::Dummy(ImVec2(0, 10));
    if (ImGui::Button("Save settings")) save_settings_now(a);
    ImGui::SetItemTooltip("Persist these settings for every next start");
    ImGui::SameLine();
    if (ImGui::Button("Reset to defaults")) a->settings = sz::Settings{};
    ImGui::SetItemTooltip("Restore defaults (not saved until you press Save)");
    if (settings_dirty(a)) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1, 0.75f, 0.2f, 1), "unsaved changes"); }
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::TextDisabled("%s", sz::settings_path().string().c_str());
}

}
