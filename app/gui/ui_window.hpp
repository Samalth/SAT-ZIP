#pragma once

struct GLFWwindow;

namespace szgui {

GLFWwindow* create_ui_window(int w, int h, const char* title, bool floating, bool resizable, bool dark);
void rebuild_fonts_if_needed();
void render_ui_frame(GLFWwindow* win);
void destroy_ui_window(GLFWwindow* win);

}
