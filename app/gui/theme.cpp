#include "theme.hpp"

#include "implot.h"

namespace szgui {

ImVec4 rgb(unsigned v, float a) {
    return ImVec4(((v >> 16) & 0xFF) / 255.0f, ((v >> 8) & 0xFF) / 255.0f, (v & 0xFF) / 255.0f, a);
}

void apply_style(bool dark) {
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    s.WindowRounding     = 0.0f;
    s.ChildRounding      = 8.0f;
    s.FrameRounding      = 4.0f;
    s.PopupRounding      = 8.0f;
    s.GrabRounding       = 4.0f;
    s.TabRounding        = 4.0f;
    s.ScrollbarRounding  = 6.0f;
    s.ScrollbarSize      = 12.0f;
    s.FramePadding       = ImVec2(12, 6);
    s.ItemSpacing        = ImVec2(8, 8);
    s.ItemInnerSpacing   = ImVec2(6, 4);
    s.WindowPadding      = ImVec2(16, 10);
    s.CellPadding        = ImVec2(8, 4);
    s.WindowBorderSize   = 0.0f;
    s.ChildBorderSize    = 1.0f;
    s.PopupBorderSize    = 1.0f;
    s.FrameBorderSize    = 1.0f;
    s.TabBorderSize      = 0.0f;
    s.TabBarBorderSize   = 1.0f;
    s.TabBarOverlineSize = 2.0f;
    s.GrabMinSize        = 12.0f;

    const ImVec4 bg      = dark ? rgb(0x202020) : rgb(0xF3F3F3);
    const ImVec4 surface = dark ? rgb(0x2B2B2B) : rgb(0xFFFFFF);
    const ImVec4 control = dark ? rgb(0x2D2D2D) : rgb(0xFFFFFF);
    const ImVec4 hover   = dark ? rgb(0x383838) : rgb(0xF0F0F0);
    const ImVec4 press   = dark ? rgb(0x323232) : rgb(0xE6E6E6);
    const ImVec4 border  = dark ? rgb(0x3D3D3D) : rgb(0xD5D5D5);
    const ImVec4 text    = dark ? rgb(0xF3F3F3) : rgb(0x1B1B1B);
    const ImVec4 muted   = dark ? rgb(0x9D9D9D) : rgb(0x6B6B6B);
    const ImVec4 accent  = dark ? rgb(0x4CC2FF) : rgb(0x0067C0);
    const ImVec4 accentS = dark ? rgb(0x4CC2FF, 0.25f) : rgb(0x0067C0, 0.18f);

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text]                  = text;
    c[ImGuiCol_TextDisabled]          = muted;
    c[ImGuiCol_WindowBg]              = bg;
    c[ImGuiCol_ChildBg]               = surface;
    c[ImGuiCol_PopupBg]               = surface;
    c[ImGuiCol_Border]                = border;
    c[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]               = control;
    c[ImGuiCol_FrameBgHovered]        = hover;
    c[ImGuiCol_FrameBgActive]         = press;
    c[ImGuiCol_TitleBg]               = bg;
    c[ImGuiCol_TitleBgActive]         = bg;
    c[ImGuiCol_TitleBgCollapsed]      = bg;
    c[ImGuiCol_MenuBarBg]             = bg;
    c[ImGuiCol_ScrollbarBg]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]         = dark ? rgb(0x4A4A4A) : rgb(0xC2C2C2);
    c[ImGuiCol_ScrollbarGrabHovered]  = dark ? rgb(0x5A5A5A) : rgb(0xA8A8A8);
    c[ImGuiCol_ScrollbarGrabActive]   = dark ? rgb(0x6A6A6A) : rgb(0x909090);
    c[ImGuiCol_CheckMark]             = accent;
    c[ImGuiCol_SliderGrab]            = accent;
    c[ImGuiCol_SliderGrabActive]      = accent;
    c[ImGuiCol_Button]                = control;
    c[ImGuiCol_ButtonHovered]         = hover;
    c[ImGuiCol_ButtonActive]          = press;
    c[ImGuiCol_Header]                = dark ? rgb(0x333333) : rgb(0xEAEAEA);
    c[ImGuiCol_HeaderHovered]         = hover;
    c[ImGuiCol_HeaderActive]          = press;
    c[ImGuiCol_Separator]             = border;
    c[ImGuiCol_SeparatorHovered]      = accent;
    c[ImGuiCol_SeparatorActive]       = accent;
    c[ImGuiCol_ResizeGrip]            = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered]     = accentS;
    c[ImGuiCol_ResizeGripActive]      = accent;
    c[ImGuiCol_Tab]                   = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TabHovered]            = hover;
    c[ImGuiCol_TabSelected]           = surface;
    c[ImGuiCol_TabSelectedOverline]   = accent;
    c[ImGuiCol_TabDimmed]             = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TabDimmedSelected]     = surface;
    c[ImGuiCol_TabDimmedSelectedOverline] = accent;
    c[ImGuiCol_PlotLines]             = accent;
    c[ImGuiCol_PlotLinesHovered]      = accent;
    c[ImGuiCol_PlotHistogram]         = accent;
    c[ImGuiCol_PlotHistogramHovered]  = accent;
    c[ImGuiCol_TableHeaderBg]         = dark ? rgb(0x2B2B2B) : rgb(0xFAFAFA);
    c[ImGuiCol_TableBorderStrong]     = border;
    c[ImGuiCol_TableBorderLight]      = dark ? rgb(0x333333) : rgb(0xE5E5E5);
    c[ImGuiCol_TableRowBg]            = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]         = dark ? rgb(0xFFFFFF, 0.025f) : rgb(0x000000, 0.025f);
    c[ImGuiCol_TextSelectedBg]        = accentS;
    c[ImGuiCol_DragDropTarget]        = accent;
    c[ImGuiCol_NavCursor]             = accent;
    c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0, 0, 0, dark ? 0.55f : 0.30f);

    ImPlotStyle& ps = ImPlot::GetStyle();
    ImPlot::StyleColorsAuto(&ps);
    ps.Colors[ImPlotCol_FrameBg] = surface;
    ps.Colors[ImPlotCol_PlotBg]  = surface;
    ps.PlotPadding = ImVec2(12, 10);
}

void push_primary_button() {
    ImGui::PushStyleColor(ImGuiCol_Button,        rgb(0x2E8B57));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, rgb(0x34A065));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  rgb(0x27774A));
    ImGui::PushStyleColor(ImGuiCol_Border,        rgb(0x2E8B57));
    ImGui::PushStyleColor(ImGuiCol_Text,          rgb(0xFFFFFF));
}

}
