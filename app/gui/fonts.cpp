#include "fonts.hpp"

#include "imgui_internal.h"

#include <filesystem>
#include <mutex>
#include <vector>

namespace fs = std::filesystem;

namespace szgui {

ImFont* g_font_bold = nullptr;
bool    g_icons = false;
ImFont* g_font_icon_big = nullptr;
int     g_font_px = 18;

static std::mutex           g_glyph_mtx;
static std::vector<bool>    g_glyph_seen(IM_UNICODE_CODEPOINT_MAX + 1);
static std::vector<ImWchar> g_glyph_extra;
std::atomic<bool>    g_fonts_dirty{false};

static bool glyph_in_base(unsigned c) {
    return c < 0x0250 || (c >= 0x0370 && c < 0x0530) || (c >= 0x2000 && c < 0x2070) || (c >= 0xE700 && c <= 0xE900);
}

void want_glyphs(const std::string& s) {
    const char* p = s.c_str();
    const char* e = p + s.size();
    std::lock_guard<std::mutex> lk(g_glyph_mtx);
    while (p < e) {
        unsigned c = 0;
        int n = ImTextCharFromUtf8(&c, p, e);
        if (n <= 0) break;
        p += n;
        if (glyph_in_base(c) || c > IM_UNICODE_CODEPOINT_MAX || g_glyph_seen[c]) continue;
        g_glyph_seen[c] = true;
        g_glyph_extra.push_back((ImWchar)c);
        g_fonts_dirty = true;
    }
}

void load_fonts() {
#ifdef _WIN32
    ImGuiIO& io = ImGui::GetIO();
    static ImVector<ImWchar> ranges;
    static const ImWchar base_ranges[] = { 0x0020, 0x024F, 0x0370, 0x052F, 0x2000, 0x206F, 0 };
    static const ImWchar icon_range[] = { 0xE700, 0xE900, 0 };
    static const char* fallbacks[] = { "msyh.ttc", "malgun.ttf", "LeelawUI.ttf", "Nirmala.ttc", "seguisym.ttf", "seguiemj.ttf" };
    ImFontGlyphRangesBuilder rb;
    rb.AddRanges(base_ranges);
    bool extra = false;
    { std::lock_guard<std::mutex> lk(g_glyph_mtx);
      for (ImWchar c : g_glyph_extra) rb.AddChar(c);
      extra = !g_glyph_extra.empty(); }
    ranges.clear();
    rb.BuildRanges(&ranges);
    auto add = [&](const char* file, float px, bool merge, const ImWchar* rg, ImVec2 off, float min_adv) -> ImFont* {
        fs::path f = fs::path("C:\\Windows\\Fonts") / file;
        if (!fs::exists(f)) return nullptr;
        ImFontConfig cfg;
        cfg.MergeMode = merge;
        cfg.GlyphOffset = off;
        cfg.GlyphMinAdvanceX = min_adv;
        return io.Fonts->AddFontFromFileTTF(f.string().c_str(), px, &cfg, rg);
    };
    const float px = (float)g_font_px;
    const float k = px / 18.0f;
    auto add_fallbacks = [&] { if (extra) for (const char* fb : fallbacks) add(fb, px, true, ranges.Data, ImVec2(0, 0), 0.0f); };
    if (add("segoeui.ttf", px, false, ranges.Data, ImVec2(0, 0), 0.0f)) {
        add_fallbacks();
        g_icons = add("segmdl2.ttf", 15.0f * k, true, icon_range, ImVec2(0, 2.0f * k), px) != nullptr;
    }
    g_font_bold = add("segoeuib.ttf", px, false, ranges.Data, ImVec2(0, 0), 0.0f);
    if (g_font_bold) add_fallbacks();
    static const ImWchar big_icon_range[] = { 0xE898, 0xE898, 0 };
    g_font_icon_big = add("segmdl2.ttf", 48.0f * k, false, big_icon_range, ImVec2(0, 0), 0.0f);
    io.Fonts->Build();
    io.Fonts->ClearInputData();
#endif
}

}
