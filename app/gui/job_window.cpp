#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  #include <windows.h>
#endif

#include "job_window.hpp"
#include "headless_jobs.hpp"
#include "ui_window.hpp"
#include "fonts.hpp"
#include "platform.hpp"
#include "theme.hpp"
#include "widgets.hpp"
#include "satzip_ops.hpp"
#include "satzip_settings.hpp"
#include "satzip_mail.hpp"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#ifdef _WIN32
  #define GLFW_EXPOSE_NATIVE_WIN32
  #include <GLFW/glfw3native.h>
#endif

#include <cstdio>
#include <functional>
#include <thread>

namespace fs = std::filesystem;

namespace szgui {

static void run_job_window(HeadlessJob& j, const std::string& name, bool close_when_done,
                           const std::function<void()>& work) {
    std::thread worker(work);
    sz::Settings cfg; sz::load_settings(cfg);
    if (!glfwInit()) { worker.join(); return; }
    float dpi = 1.0f;
    if (GLFWmonitor* mon = glfwGetPrimaryMonitor()) { float sy = 1.0f; glfwGetMonitorContentScale(mon, &dpi, &sy); }
    if (dpi < 1.0f) dpi = 1.0f;
    g_font_px = (int)(18.0f * dpi + 0.5f);
    int ww = (int)(580 * dpi), wh = (int)(220 * dpi);
    GLFWwindow* win = create_ui_window(ww, wh, "SAT-ZIP", true, false, cfg.dark);
    if (!win) { glfwTerminate(); worker.join(); return; }
    ImGui::GetIO().IniFilename = nullptr;
    if (GLFWmonitor* mon = glfwGetPrimaryMonitor()) {
        int mx, my, mw, mh;
        glfwGetMonitorWorkarea(mon, &mx, &my, &mw, &mh);
        glfwSetWindowPos(win, mx + (mw - ww) / 2, my + (mh - wh) / 2);
    }
    glfwShowWindow(win);
    glfwFocusWindow(win);
#ifdef _WIN32
    if (HWND hw = glfwGetWin32Window(win)) SetForegroundWindow(hw);
#endif
    want_glyphs(name);

    bool grown = false, open_dest = false;
    while (true) {
        glfwWaitEventsTimeout(1.0 / 30.0);
        bool finished = j.finished;
        if (glfwWindowShouldClose(win)) {
            if (finished) break;
            j.cancel = true;
            glfwSetWindowShouldClose(win, GLFW_FALSE);
        }
        if (finished && close_when_done && j.packed) break;
        if (finished && !grown && !j.failed.empty()) {
            glfwSetWindowSize(win, ww, wh + (int)(150 * dpi));
            grown = true;
        }
        rebuild_fonts_if_needed();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("job", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoSavedSettings);
        ImGui::Dummy(ImVec2(0, 4));

        std::string title, detail;
        bool error = finished && !j.error.empty();
        bool cancelled = finished && !error && (j.create ? !j.packed : (bool)j.cancel);
        bool offer_open = false;
        if (!finished) {
            title = (j.cancel ? std::string("Cancelling...") : (j.create ? "Compressing " : "Extracting ") + name);
            detail = j.total == 0 ? (j.create ? "Scanning files..." : "Reading archive...")
                                  : "To: " + j.dest.u8string();
        } else if (error) {
            title = (j.create ? "Could not create an archive of " : "Could not extract ") + name;
            detail = j.error;
        } else if (j.create) {
            if (cancelled) { title = "Archive creation cancelled"; detail = "No archive was written."; }
            else {
                char s[160];
                snprintf(s, sizeof(s), "%d file(s), %s -> %s (x%.2f)", j.files, fmt_size(j.raw).c_str(),
                         fmt_size(j.packed).c_str(), j.packed ? (double)j.raw / j.packed : 0.0);
                title = "Archive created";
                detail = j.dest.u8string() + "\n" + s;
                offer_open = true;
            }
        } else {
            title = cancelled ? "Extraction cancelled" : j.bad ? "Extraction finished with errors" : "Extraction complete";
            detail = std::to_string(j.files) + " file(s) extracted to:\n" + j.dest.u8string();
            if (j.bad && !cancelled)
                detail += "\n" + std::to_string(j.bad) + " item(s) could not be extracted (folder access, a name collision, or path length?).";
            offer_open = j.files > 0;
        }
        want_glyphs(detail);

        if (g_font_bold) ImGui::PushFont(g_font_bold);
        if (error || (finished && j.bad))
            ImGui::TextColored(cfg.dark ? rgb(0xFF99A4) : rgb(0xC42B1C), "%s", title.c_str());
        else
            ImGui::TextUnformatted(title.c_str());
        if (g_font_bold) ImGui::PopFont();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", detail.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 4));

        if (!finished) {
            size_t total = j.total, done = j.done;
            if (total) {
                char pct[48];
                snprintf(pct, sizeof(pct), j.create ? "%d%%" : "%d%%   (%zu of %zu items)",
                         (int)(done * 100 / total), done, total);
                ImGui::ProgressBar((float)done / (float)total, ImVec2(-1, ImGui::GetFrameHeight()), pct);
            } else {
                ImGui::ProgressBar(-1.0f * (float)ImGui::GetTime(), ImVec2(-1, ImGui::GetFrameHeight()), "");
            }
        } else if (!j.failed.empty()) {
            ImGui::TextUnformatted("Failed items:");
            ImGui::BeginChild("failed", ImVec2(0, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() - 4),
                              ImGuiChildFlags_Borders);
            for (const auto& f : j.failed) ImGui::TextUnformatted(f.c_str());
            ImGui::EndChild();
        }

        ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ImGui::GetStyle().WindowPadding.y - ImGui::GetFrameHeight());
        const ImGuiStyle& st = ImGui::GetStyle();
        if (!finished) {
            right_align(ImGui::CalcTextSize("Cancel").x + st.FramePadding.x * 2.0f);
            if (j.cancel) ImGui::BeginDisabled();
            if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) j.cancel = true;
            if (j.cancel) ImGui::EndDisabled();
        } else {
            const char* open_label = j.create ? "Show in folder" : "Open folder";
            float w = ImGui::CalcTextSize("Close").x + st.FramePadding.x * 2.0f;
            if (offer_open) w += ImGui::CalcTextSize(open_label).x + st.FramePadding.x * 2.0f + st.ItemSpacing.x;
            right_align(w);
            if (offer_open) {
                push_primary_button();
                if (ImGui::Button(open_label) || ImGui::IsKeyPressed(ImGuiKey_Enter)) open_dest = true;
                ImGui::PopStyleColor(5);
                ImGui::SameLine();
            }
            if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) glfwSetWindowShouldClose(win, GLFW_TRUE);
            if (open_dest) glfwSetWindowShouldClose(win, GLFW_TRUE);
        }
        ImGui::End();
        render_ui_frame(win);
    }
    destroy_ui_window(win);
    glfwTerminate();
    j.cancel = j.cancel || !j.finished;
    worker.join();
    if (open_dest) open_in_explorer(j.dest);
}

int run_headless_zip(const char* path, bool mail) {
    HeadlessJob j;
    j.create = true;
    run_job_window(j, fs::path(path).filename().u8string(), mail, [&] { headless_zip_job(path, j); });
    if (j.packed && mail) sz::mail_with_attachment(j.dest);
    return j.packed ? 0 : 1;
}

int run_headless_extract(const char* archive, bool subfolder) {
    HeadlessJob j;
    run_job_window(j, fs::path(archive).filename().u8string(), false,
                   [&] { headless_extract_job(archive, subfolder, j); });
    return (j.bad || !j.error.empty()) ? 1 : 0;
}

}
