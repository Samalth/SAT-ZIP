#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  #include <windows.h>
#endif

#include "tabs.hpp"
#include "archive_ops.hpp"
#include "fonts.hpp"
#include "platform.hpp"
#include "theme.hpp"
#include "widgets.hpp"

#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <cstring>

namespace szgui {

static void draw_drop_zone(App* a) {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1(p0.x + avail.x, p0.y + avail.y);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImGuiStyle& st = ImGui::GetStyle();
    dl->AddRectFilled(p0, p1, ImGui::GetColorU32(ImGuiCol_ChildBg), 10.0f);
    ImU32 dash = ImGui::GetColorU32(ImGuiCol_Border);
    const float seg = 10.0f, gap = 6.0f, in = 1.0f;
    for (float x = p0.x + 12.0f; x < p1.x - 12.0f; x += seg + gap) {
        float x2 = x + seg < p1.x - 12.0f ? x + seg : p1.x - 12.0f;
        dl->AddLine(ImVec2(x, p0.y + in), ImVec2(x2, p0.y + in), dash, 1.5f);
        dl->AddLine(ImVec2(x, p1.y - in), ImVec2(x2, p1.y - in), dash, 1.5f);
    }
    for (float y = p0.y + 12.0f; y < p1.y - 12.0f; y += seg + gap) {
        float y2 = y + seg < p1.y - 12.0f ? y + seg : p1.y - 12.0f;
        dl->AddLine(ImVec2(p0.x + in, y), ImVec2(p0.x + in, y2), dash, 1.5f);
        dl->AddLine(ImVec2(p1.x - in, y), ImVec2(p1.x - in, y2), dash, 1.5f);
    }

    const char* title = "Drop files or folders here";
    const char* sub   = "to create an archive - or drop an archive (.satz .zip .7z .tar .tar.xz) to open it";
    const char* b1 = "Add files...";
    const char* b2 = "Add folder...";
    const char* b3 = "Open archive...";
    ImFont* icon = g_font_icon_big;
    float icon_h = icon ? icon->FontSize : 0.0f;
    if (g_font_bold) ImGui::PushFont(g_font_bold);
    ImVec2 ts = ImGui::CalcTextSize(title);
    if (g_font_bold) ImGui::PopFont();
    ImVec2 ss = ImGui::CalcTextSize(sub);
    float bw = ImGui::CalcTextSize(b1).x + ImGui::CalcTextSize(b2).x + ImGui::CalcTextSize(b3).x +
               st.FramePadding.x * 6.0f + st.ItemSpacing.x * 2.0f;
    float block_h = icon_h + 12.0f + ts.y + 4.0f + ss.y + 20.0f + ImGui::GetFrameHeight();
    float y = p0.y + (avail.y - block_h) * 0.45f;
    float cx = p0.x + avail.x * 0.5f;
    if (icon) {
        ImVec2 is = icon->CalcTextSizeA(icon->FontSize, FLT_MAX, 0.0f, SZ_ICON_DROP);
        dl->AddText(icon, icon->FontSize, ImVec2(cx - is.x * 0.5f, y), ImGui::GetColorU32(ImGuiCol_CheckMark), SZ_ICON_DROP);
        y += icon_h + 12.0f;
    }
    ImGui::SetCursorScreenPos(ImVec2(cx - ts.x * 0.5f, y));
    if (g_font_bold) ImGui::PushFont(g_font_bold);
    ImGui::TextUnformatted(title);
    if (g_font_bold) ImGui::PopFont();
    y += ts.y + 4.0f;
    ImGui::SetCursorScreenPos(ImVec2(cx - ss.x * 0.5f, y));
    ImGui::TextDisabled("%s", sub);
    y += ss.y + 20.0f;
    ImGui::SetCursorScreenPos(ImVec2(cx - bw * 0.5f, y));
    if (ImGui::Button(b1)) { std::string p; if (dlg_open(p, false)) stage_paths(a, {p}); }
    ImGui::SameLine();
    if (ImGui::Button(b2)) { std::string p; if (dlg_folder(p)) stage_paths(a, {p}); }
    ImGui::SameLine();
    if (ImGui::Button(b3)) { std::string p; if (dlg_open(p, true)) stage_paths(a, {p}); }
    ImGui::SetCursorScreenPos(p1);
    ImGui::Dummy(ImVec2(0, 0));
}

void draw_archive_tab(App* a) {
    bool busy = a->running;
    if (a->mode == Mode::Empty) {
        if (busy) ImGui::BeginDisabled();
        draw_drop_zone(a);
        if (busy) ImGui::EndDisabled();
        return;
    }
    if (busy) ImGui::BeginDisabled();

    if (ImGui::Button("Open Archive...")) { std::string p; if (dlg_open(p, true)) stage_paths(a, {p}); }
    ImGui::SetItemTooltip("Open a .satz, .zip, .7z, .tar or .tar.xz archive");
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) ImGui::OpenPopup("browse");
    ImGui::SetItemTooltip("Add files or a folder to a new archive");
    if (ImGui::BeginPopup("browse")) {
        if (ImGui::MenuItem("Add files..."))  { std::string p; if (dlg_open(p, false)) stage_paths(a, {p}); }
        if (ImGui::MenuItem("Add folder...")) { std::string p; if (dlg_folder(p))      stage_paths(a, {p}); }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) { std::lock_guard<std::mutex> lk(a->mtx);
        a->staged.clear(); a->suggest_name.clear(); a->opened.close(); a->selected.clear();
        a->ventries.clear(); a->view_order.clear(); a->mode = Mode::Empty;
        a->status = "Cleared."; }
    ImGui::SetItemTooltip("Clear the staged items / close the opened archive");

    if (a->mode == Mode::Building) {
        if (g_font_bold) ImGui::PushFont(g_font_bold);
        const float btnW = ImGui::CalcTextSize("Create Archive").x + ImGui::GetStyle().FramePadding.x * 4.0f;
        if (g_font_bold) ImGui::PopFont();
        ImGui::SameLine();
        float target = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - btnW;
        if (target > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(target);
        push_primary_button();
        if (g_font_bold) ImGui::PushFont(g_font_bold);
        bool create = ImGui::Button("Create Archive", ImVec2(btnW, 0));
        if (g_font_bold) ImGui::PopFont();
        ImGui::PopStyleColor(5);
        if (create) {
            std::string suggest = "archive";
            int deffmt;
            { std::lock_guard<std::mutex> lk(a->mtx);
              if (!a->suggest_name.empty()) suggest = a->suggest_name;
              deffmt = a->settings.format; }
            std::string out;
            if (dlg_save(out, suggest, deffmt)) launch(a, std::thread(do_create, a, out));
        }
        ImGui::SetItemTooltip("Compress the staged items into one .satz archive");
    } else if (a->mode == Mode::Viewing) {
        ImGui::SameLine(0, 24);
        if (ImGui::Button("Extract All...")) {
            std::string dst; if (dlg_folder(dst)) launch(a, std::thread(do_extract, a, dst, false));
        }
        ImGui::SetItemTooltip("Extract every entry to a folder");
        ImGui::SameLine();
        if (ImGui::Button("Extract Selected...")) {
            std::string dst; if (dlg_folder(dst)) launch(a, std::thread(do_extract, a, dst, true));
        }
        ImGui::SetItemTooltip("Extract only the checked entries");
        if (a->okind != AKind::SevenZ && a->okind != AKind::Tar) {
            wrap_same_line(160);
            if (ImGui::Button("Add to archive...")) ImGui::OpenPopup("editadd");
            ImGui::SetItemTooltip("Add files or a folder to this archive");
            if (ImGui::BeginPopup("editadd")) {
                if (ImGui::MenuItem("Add files...")) { std::string p; if (dlg_open(p, false)) launch(a, std::thread(do_edit, a, std::vector<std::string>{p}, false)); }
                if (ImGui::MenuItem("Add folder...")) { std::string p; if (dlg_folder(p)) launch(a, std::thread(do_edit, a, std::vector<std::string>{p}, false)); }
                ImGui::EndPopup();
            }
            wrap_same_line(150);
            if (ImGui::Button("Remove selected")) launch(a, std::thread(do_edit, a, std::vector<std::string>{}, true));
            ImGui::SetItemTooltip("Rebuild the archive without the checked entries");
        }
    }
    if (busy) ImGui::EndDisabled();

    bool editable = a->mode == Mode::Building || (a->okind != AKind::SevenZ && a->okind != AKind::Tar);
    if (editable) {
    if (busy) ImGui::BeginDisabled();
    const char* levels[] = {"1 (fastest)","2","3","4","5","6","7","8","9 (smallest)"};
    int li = a->settings.level - 1; if (li < 0) li = 0; if (li > 8) li = 8;
    ImGui::SetNextItemWidth(150);
    if (ImGui::Combo("compression", &li, levels, 9)) a->settings.level = li + 1;
    wrap_same_line(240); ImGui::SetNextItemWidth(120);
    ImGui::SliderInt("threads", &a->settings.threads, 0, 32, a->settings.threads == 0 ? "auto" : "%d");
    wrap_same_line(300);
    ImGui::Checkbox("compress already-compressed files", &a->settings.compress_all);
    if (!a->obelisk.empty()) {
        wrap_same_line(240);
        ImGui::Checkbox("encrypt with SAT-OBELISK", &a->obelisk_after);
        ImGui::SetItemTooltip("After creating the archive, open it in SAT-OBELISK to encrypt it.\n"
                              "Your password stays in SAT-OBELISK; SAT-ZIP never sees it.");
    }
    wrap_same_line(60);
    ImGui::Checkbox(g_icons ? SZ_ICON_MAIL "  email##email" : "email##email", &a->mail_after);
    ImGui::SetItemTooltip("Email: after creating the archive, open a new email with it attached.");
    if (busy) ImGui::EndDisabled();
    }

    ImGui::Separator();

    std::lock_guard<std::mutex> lk(a->mtx);
    if (a->mode == Mode::Building) {
        ImGui::Text("New archive - %zu item(s) staged", a->staged.size());
        if (ImGui::BeginTable("staged", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                           ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY, ImVec2(0, ImGui::GetContentRegionAvail().y))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthStretch, 4.0f);
            ImGui::TableSetupColumn("size", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableHeadersRow();
            if (ImGuiTableSortSpecs* ss = ImGui::TableGetSortSpecs()) {
                if (ss->SpecsDirty && ss->SpecsCount > 0) {
                    const ImGuiTableColumnSortSpecs& sp = ss->Specs[0];
                    bool asc = sp.SortDirection != ImGuiSortDirection_Descending;
                    std::stable_sort(a->staged.begin(), a->staged.end(), [&](const sz::Item& x, const sz::Item& y) {
                        bool lt = (sp.ColumnIndex == 1) ? (x.size < y.size) : (x.name < y.name);
                        bool gt = (sp.ColumnIndex == 1) ? (x.size > y.size) : (x.name > y.name);
                        return asc ? lt : gt;
                    });
                    ss->SpecsDirty = false;
                }
            }
            ImGuiListClipper clip;
            clip.Begin((int)a->staged.size());
            while (clip.Step()) {
                for (int idx = clip.DisplayStart; idx < clip.DisplayEnd; ++idx) {
                    const sz::Item& it = a->staged[(size_t)idx];
                    ImGui::TableNextRow(); ImGui::TableNextColumn();
                    ImGui::PushID(idx);
                    std::string label = it.name + (it.is_dir ? "/" : "");
                    ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns);
                    if (ImGui::BeginPopupContextItem("ctx")) {
                        if (ImGui::MenuItem("Open in Explorer")) open_in_explorer(it.abs);
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                    ImGui::TableNextColumn();
                    if (it.is_dir) ImGui::TextDisabled("-"); else ImGui::TextUnformatted(fmt_size(it.size).c_str());
                }
            }
            ImGui::EndTable();
        }
    } else if (a->mode == Mode::Viewing) {
        ImGui::Text("Viewing %s - %zu entries%s", fs::path(a->opened_path).filename().string().c_str(),
                    a->ventries.size(), a->okind == AKind::Zip ? "  [zip]" :
                                        a->okind == AKind::SevenZ ? "  [7z]" :
                                        a->okind == AKind::Tar ? "  [tar]" : "");
        ImGui::SameLine(0, 24);
        ImGui::SetNextItemWidth(260);
        ImGui::InputTextWithHint("##search", "Search in archive...", a->search, sizeof(a->search));
        want_glyphs(a->search);
        ImGui::SetItemTooltip("Filter entries by name (case-insensitive). Double-click an entry to open it.");
        if (ImGui::BeginTable("entries", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                            ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY, ImVec2(0, ImGui::GetContentRegionAvail().y))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, ImGui::GetFrameHeight());
            ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthStretch, 4.0f);
            ImGui::TableSetupColumn("size", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("stored", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableHeadersRow();
            bool refilter = false;
            if (a->filter_applied != a->search) {
                std::string f = a->search;
                std::string flc = f;
                for (auto& c : flc) c = (char)tolower((unsigned char)c);
                a->view_order.clear();
                for (uint32_t i = 0; i < a->ventries.size(); ++i) {
                    if (flc.empty()) { a->view_order.push_back(i); continue; }
                    std::string nlc = a->ventries[i].name;
                    for (auto& c : nlc) c = (char)tolower((unsigned char)c);
                    if (nlc.find(flc) != std::string::npos) a->view_order.push_back(i);
                }
                a->filter_applied = f;
                refilter = true;
            }
            if (ImGuiTableSortSpecs* ss = ImGui::TableGetSortSpecs()) {
                if ((ss->SpecsDirty || refilter) && ss->SpecsCount > 0) {
                    const ImGuiTableColumnSortSpecs& sp = ss->Specs[0];
                    bool asc = sp.SortDirection != ImGuiSortDirection_Descending;
                    const std::vector<ViewEntry>& es = a->ventries;
                    std::stable_sort(a->view_order.begin(), a->view_order.end(), [&](uint32_t x, uint32_t y) {
                        const ViewEntry& ex = es[x]; const ViewEntry& ey = es[y];
                        bool lt, gt;
                        if (sp.ColumnIndex == 2)      { lt = ex.orig   < ey.orig;   gt = ex.orig   > ey.orig; }
                        else if (sp.ColumnIndex == 3) { lt = ex.stored < ey.stored; gt = ex.stored > ey.stored; }
                        else { int c = ex.name.compare(ey.name); lt = c < 0; gt = c > 0; }
                        return asc ? lt : gt;
                    });
                    ss->SpecsDirty = false;
                }
            }
            ImGuiListClipper clip;
            clip.Begin((int)a->view_order.size());
            while (clip.Step()) {
                for (int k = clip.DisplayStart; k < clip.DisplayEnd; ++k) {
                    uint32_t i = a->view_order[(size_t)k];
                    const ViewEntry& e = a->ventries[i];
                    ImGui::TableNextRow(); ImGui::TableNextColumn();
                    if (!e.is_dir) { bool c = a->selected[i] != 0; ImGui::PushID((int)i);
                        if (ImGui::Checkbox("##s", &c)) a->selected[i] = c ? 1 : 0; ImGui::PopID(); }
                    ImGui::TableNextColumn();
                    ImGui::PushID((int)i + 100000);
                    std::string lbl = e.name + (e.is_dir ? "/" : "");
                    ImGui::Selectable(lbl.c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap);
                    if (!e.is_dir && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && !a->running)
                        launch(a, std::thread(do_open_entry, a, i));
                    ImGui::PopID();
                    ImGui::TableNextColumn();
                    if (e.is_dir) ImGui::TextDisabled("-"); else ImGui::TextUnformatted(fmt_size(e.orig).c_str());
                    ImGui::TableNextColumn();
                    if (e.is_dir) ImGui::TextDisabled("-"); else ImGui::TextUnformatted(fmt_size(e.stored).c_str());
                }
            }
            ImGui::EndTable();
        }
    }
}

}
