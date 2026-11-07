#pragma once

#include "imgui.h"

#include <atomic>
#include <string>

#define SZ_ICON_MAIL   "\xEE\x9C\x95"
#define SZ_ICON_FILTER "\xEE\x9C\x9C"
#define SZ_ICON_DROP   "\xEE\xA2\x98"
#define SZ_ICON_BACK   "\xEE\x9C\xAB"
#define SZ_ICON_FWD    "\xEE\x9C\xAA"
#define SZ_ICON_UP     "\xEE\x9D\x8A"
#define SZ_ICON_FOLDER "\xEF\x84\xAB"
#define SZ_ICON_FILE   "\xEE\x9F\x83"
#define SZ_ICON_CHEVRON "\xEE\x9D\xAC"

namespace szgui {

extern ImFont*           g_font_bold;
extern bool              g_icons;
extern ImFont*           g_font_icon_big;
extern int               g_font_px;
extern std::atomic<bool> g_fonts_dirty;

void want_glyphs(const std::string& s);
void load_fonts();

}
