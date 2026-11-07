#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  #include <windows.h>
#endif

#include "viewer_window.hpp"
#include "archive_browser.hpp"
#include "archive_ops.hpp"
#include "fonts.hpp"
#include "status_bar.hpp"
#include "ui_window.hpp"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

namespace szgui {

int run_archive_viewer(const char* path) {
    sz::Settings cfg;
    sz::load_settings(cfg);
    if (!glfwInit()) return 1;
    float dpi = 1.0f;
    if (GLFWmonitor* mon = glfwGetPrimaryMonitor()) { float sy = 1.0f; glfwGetMonitorContentScale(mon, &dpi, &sy); }
    if (dpi < 1.0f) dpi = 1.0f;
    g_font_px = (int)(16.0f * dpi + 0.5f);
    const std::string name = fs::path(path).filename().u8string();
    const std::string title = name + " - SAT-ZIP";
    GLFWwindow* win = create_ui_window((int)(1000 * dpi), (int)(640 * dpi), title.c_str(), false, true, cfg.dark);
    if (!win) { glfwTerminate(); return 1; }
    ImGui::GetIO().IniFilename = nullptr;
    want_glyphs(name);

    App app;
    app.win = win;
    app.settings = cfg;
    app.settings_saved = cfg;
    open_path(&app, path);

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        rebuild_fonts_if_needed();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("viewer", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse);

        BrowserAction act;
        const bool busy = app.running;
        const float body_h = ImGui::GetContentRegionAvail().y - footer_height(&app) - ImGui::GetStyle().ItemSpacing.y;
        ImGui::BeginChild("body", ImVec2(0, body_h), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
        {
            std::lock_guard<std::mutex> lk(app.mtx);
            if (app.mode == Mode::Viewing) {
                if (busy) ImGui::BeginDisabled();
                draw_extract_buttons(&app, act);
                if (busy) ImGui::EndDisabled();
                ImGui::Separator();
                draw_archive_browser(&app, act);
            } else if (!busy) {
                ImGui::Dummy(ImVec2(0, 20));
                ImGui::TextUnformatted(app.status.c_str());
            }
        }
        ImGui::EndChild();
        ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ImGui::GetStyle().WindowPadding.y - footer_height(&app));
        ImGui::Separator();
        draw_footer(&app);
        ImGui::End();
        render_ui_frame(win);
        run_browser_action(&app, act);
    }

    app.cancel = true;
    join_worker(&app);
    destroy_ui_window(win);
    glfwTerminate();
    return 0;
}

}
