#include "ui_window.hpp"
#include "fonts.hpp"
#include "platform.hpp"
#include "theme.hpp"

#include "imgui.h"
#include "implot.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

namespace szgui {

GLFWwindow* create_ui_window(int w, int h, const char* title, bool floating, bool resizable, bool dark) {
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    glfwWindowHint(GLFW_FLOATING, floating ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, resizable ? GLFW_TRUE : GLFW_FALSE);
    GLFWwindow* win = glfwCreateWindow(w, h, title, nullptr, nullptr);
    if (!win) return nullptr;
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
    set_window_icon(win);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    load_fonts();
    apply_style(dark);
    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init("#version 130");
    return win;
}

void rebuild_fonts_if_needed() {
    if (!g_fonts_dirty.exchange(false)) return;
    ImGui::GetIO().Fonts->Clear();
    load_fonts();
    ImGui_ImplOpenGL3_DestroyFontsTexture();
    ImGui_ImplOpenGL3_CreateFontsTexture();
}

void render_ui_frame(GLFWwindow* win) {
    ImGui::Render();
    int w, h; glfwGetFramebufferSize(win, &w, &h);
    glViewport(0, 0, w, h);
    const ImVec4 clear = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
    glClearColor(clear.x, clear.y, clear.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(win);
}

void destroy_ui_window(GLFWwindow* win) {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(win);
}

}
