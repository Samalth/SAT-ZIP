#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  #include <windows.h>
#endif

#include "app_state.hpp"
#include "archive_ops.hpp"
#include "tabs.hpp"
#include "job_window.hpp"
#include "ui_window.hpp"
#include "fonts.hpp"
#include "theme.hpp"
#include "platform.hpp"
#include "satzip_mail.hpp"
#ifdef _WIN32
#include "satzip_share.hpp"
#endif

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#ifdef _WIN32
  #define GLFW_EXPOSE_NATIVE_WIN32
  #include <GLFW/glfw3native.h>
#endif

namespace szgui {

static void draw_footer(App* a) {
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

static float footer_height(const App* a) {
    const ImGuiStyle& st = ImGui::GetStyle();
    float rows = a->running ? ImGui::GetFrameHeight() + st.ItemSpacing.y + ImGui::GetTextLineHeight()
                            : ImGui::GetFrameHeight();
    return rows + st.ItemSpacing.y + 1.0f;
}

#ifdef _WIN32
static WNDPROC g_prev_wndproc = nullptr;
static App*    g_app = nullptr;
static LRESULT CALLBACK sz_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_COPYDATA && g_app) {
        COPYDATASTRUCT* cds = (COPYDATASTRUCT*)lp;
        if (cds && cds->lpData && cds->cbData) {
            std::string path((const char*)cds->lpData, cds->cbData - 1);
            stage_paths(g_app, { path });
            SetForegroundWindow(h);
        }
        return TRUE;
    }
    return g_prev_wndproc ? CallWindowProcW(g_prev_wndproc, h, msg, wp, lp)
                          : DefWindowProcW(h, msg, wp, lp);
}
static bool forward_to_existing(const std::string& path) {
    HWND other = FindWindowW(nullptr, L"SAT-ZIP");
    if (!other) return false;
    COPYDATASTRUCT cds;
    cds.dwData = 1;
    cds.cbData = (DWORD)path.size() + 1;
    cds.lpData = (void*)path.c_str();
    SendMessageW(other, WM_COPYDATA, 0, (LPARAM)&cds);
    return true;
}
#endif

static void drop_cb(GLFWwindow* w, int count, const char** paths) {
    App* a = (App*)glfwGetWindowUserPointer(w);
    if (a->running) return;
    std::vector<std::string> v; for (int i = 0; i < count; ++i) v.emplace_back(paths[i]);
    stage_paths(a, v);
}

}

using namespace szgui;

int main(int argc, char** argv) {
    install_crash_log();
    if (argc >= 3 && std::string(argv[1]) == "--zip")     return run_headless_zip(argv[2], false);
    if (argc >= 3 && std::string(argv[1]) == "--zipmail") return run_headless_zip(argv[2], true);
    if (argc >= 3 && std::string(argv[1]) == "--xhere") return run_headless_extract(argv[2], false);
    if (argc >= 3 && std::string(argv[1]) == "--xto")   return run_headless_extract(argv[2], true);
    std::string launch_path = (argc >= 2) ? std::string(argv[1]) : std::string();

#ifdef _WIN32
    HANDLE inst_mtx = CreateMutexW(nullptr, FALSE, L"Local\\SATZIP_GUI_Instance");
    if (inst_mtx && GetLastError() == ERROR_ALREADY_EXISTS && !launch_path.empty()) {
        if (forward_to_existing(launch_path)) return 0;
    }
#endif

    sz::Settings settings; sz::load_settings(settings);

    if (!glfwInit()) return 1;
    GLFWwindow* win = create_ui_window(settings.win_w, settings.win_h, "SAT-ZIP", false, true, settings.dark);
    if (!win) { glfwTerminate(); return 1; }

    App app; app.win = win; app.settings = settings; app.settings_saved = settings;
    glfwSetWindowUserPointer(win, &app);
    glfwSetDropCallback(win, drop_cb);

#ifdef _WIN32
    g_app = &app;
    {
        HWND hwnd = glfwGetWin32Window(win);
        if (hwnd) g_prev_wndproc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)sz_wndproc);
    }
#endif
    if (!launch_path.empty()) stage_paths(&app, { launch_path });

    int prev_tab = 0;
    int pending_px = g_font_px;
    double pending_since = 0.0;
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        {
            int ww = 0, wh = 0;
            glfwGetWindowSize(win, &ww, &wh);
            float scale = (float)wh / 760.0f;
            if (scale < 0.8f) scale = 0.8f;
            if (scale > 2.0f) scale = 2.0f;
            int want_px = (int)(18.0f * scale + 0.5f);
            double now = glfwGetTime();
            if (want_px != pending_px) { pending_px = want_px; pending_since = now; }
            else if (want_px != g_font_px && now - pending_since > 0.15) { g_font_px = want_px; g_fonts_dirty = true; }
            ImGui::GetIO().FontGlobalScale = (float)want_px / (float)g_font_px;
        }
        rebuild_fonts_if_needed();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("SAT-ZIP", nullptr,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        int cur_tab = prev_tab;
        const float body_h = -(footer_height(&app) + ImGui::GetStyle().ItemSpacing.y);
        auto tab_body = [&](void (*draw)(App*)) {
            ImGui::BeginChild("body", ImVec2(0, body_h), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
            ImGui::Dummy(ImVec2(0, 4));
            draw(&app);
            ImGui::EndChild();
        };
        if (ImGui::BeginTabBar("tabs", ImGuiTabBarFlags_DrawSelectedOverline)) {
            static const char* tab_names[4] = { "Archive", "Benchmark", "Settings", "Info" };
            float tabw = 0.0f;
            if (g_font_bold) ImGui::PushFont(g_font_bold);
            for (const char* n : tab_names) { float w = ImGui::CalcTextSize(n).x; if (w > tabw) tabw = w; }
            if (g_font_bold) ImGui::PopFont();
            tabw += ImGui::GetStyle().FramePadding.x * 2.0f + 8.0f;
            auto tab_begin = [&](const char* name, int id) {
                bool bold = g_font_bold && prev_tab == id;
                ImGui::SetNextItemWidth(tabw);
                if (bold) ImGui::PushFont(g_font_bold);
                bool open = ImGui::BeginTabItem(name);
                if (bold) ImGui::PopFont();
                return open;
            };
            if (tab_begin("Archive", 0))   { cur_tab = 0; tab_body(draw_archive_tab);   ImGui::EndTabItem(); }
            if (tab_begin("Benchmark", 1)) { cur_tab = 1; tab_body(draw_benchmark_tab); ImGui::EndTabItem(); }
            if (tab_begin("Settings", 2))  { cur_tab = 2; tab_body(draw_settings_tab);  ImGui::EndTabItem(); }
            if (tab_begin("Info", 3))      { cur_tab = 3; tab_body(draw_info_tab);      ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
        if (prev_tab == 2 && cur_tab != 2 && settings_dirty(&app))
            ImGui::OpenPopup("Unsaved settings");
        prev_tab = cur_tab;
        if (ImGui::BeginPopupModal("Unsaved settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("You changed settings without saving.");
            ImGui::Dummy(ImVec2(0, 6));
            if (ImGui::Button("Save", ImVec2(120, 0))) { save_settings_now(&app); ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button("Discard", ImVec2(120, 0))) {
                app.settings = app.settings_saved;
                apply_style(app.settings.dark);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (app.share_pending.exchange(false)) {
            std::string sp;
            { std::lock_guard<std::mutex> lk(app.mtx); sp = app.share_path; }
#ifdef _WIN32
            HWND hw = glfwGetWin32Window(win);
            bool shared = hw && sz::share_file(hw, fs::path(sp).wstring());
#else
            bool shared = false;
#endif
            std::lock_guard<std::mutex> lk(app.mtx);
            if (shared) {
                app.status += "  Choose an app (e.g. Outlook) in the share panel to email the archive.";
            } else {
                sz::MailResult mr = sz::mail_fallback_compose(fs::path(sp));
                if (mr == sz::MailResult::ClipboardCompose)
                    app.status += "  Mail opened; the archive is on your clipboard - press Ctrl+V to attach it.";
                else if (mr == sz::MailResult::Failed)
                    app.status += "  (could not open a mail client)";
            }
        }
        ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ImGui::GetStyle().WindowPadding.y - footer_height(&app));
        ImGui::Separator();
        draw_footer(&app);
        ImGui::End();

        render_ui_frame(win);
    }

    if (app.worker.joinable()) app.worker.join();
    destroy_ui_window(win);
    glfwTerminate();
    return 0;
}
