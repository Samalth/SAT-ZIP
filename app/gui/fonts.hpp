#pragma once

#include "imgui.h"

#include <atomic>
#include <string>

#define SZ_ICON_MAIL   "\xEE\x9C\x95"
#define SZ_ICON_FILTER "\xEE\x9C\x9C"
#define SZ_ICON_DROP   "\xEE\xA2\x98"

namespace szgui {

extern ImFont*           g_font_bold;
extern bool              g_icons;
extern ImFont*           g_font_icon_big;
extern int               g_font_px;
extern std::atomic<bool> g_fonts_dirty;

void want_glyphs(const std::string& s);
void load_fonts();

}
