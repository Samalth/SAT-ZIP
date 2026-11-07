#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  #include <windows.h>
#endif

#include "archive_browser.hpp"
#include "archive_ops.hpp"
#include "fonts.hpp"
#include "platform.hpp"
#include "theme.hpp"
#include "widgets.hpp"
#include "satzip_any.hpp"

#include "imgui.h"

#include <cfloat>
#include <chrono>
#include <cstring>
#include <fstream>

namespace szgui {

static bool nav_button(const char* icon, const char* fallback, const char* id, bool enabled, const char* tip) {
    if (!enabled) ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    std::string label = std::string(g_icons ? icon : fallback) + "##" + id;
    bool pressed = ImGui::Button(label.c_str());
    ImGui::PopStyleColor(2);
    if (!enabled) ImGui::EndDisabled();
    ImGui::SetItemTooltip("%s", tip);
    return pressed && enabled;
}

void draw_extract_buttons(App* a, BrowserAction& act) {
    size_t n = tree_selected_count(a->tree);
    std::string label = n ? "Extract " + std::to_string(n) + (n == 1 ? " item" : " items") : std::string("Extract all");
    push_primary_button();
    if (ImGui::Button((label + "##extract").c_str())) act.cmd = BrowserCmd::ExtractHere;
    ImGui::PopStyleColor(5);
    ImGui::SetItemTooltip(n ? "Extract the selected items next to the archive" :
                              "Extract everything next to the archive (Extract Here)");
    ImGui::SameLine();
    if (ImGui::Button("Extract to...")) act.cmd = BrowserCmd::ExtractTo;
    ImGui::SetItemTooltip("Choose a folder. Uses the selection, or everything when nothing is selected.");
}

void draw_archive_browser(App* a, BrowserAction& act) {
    ArchiveTree& t = a->tree;
    if (t.rev != a->ventries_rev) tree_build(t, a->ventries, a->ventries_rev);
    ImGuiIO& io = ImGui::GetIO();
    int go_to = -1;

    if (nav_button(SZ_ICON_BACK, "<", "back", !t.back.empty(), "Back (Alt+Left)")) tree_back(t);
    ImGui::SameLine(0, 2);
    if (nav_button(SZ_ICON_FWD, ">", "fwd", !t.forward.empty(), "Forward (Alt+Right)")) tree_forward(t);
    ImGui::SameLine(0, 2);
    if (nav_button(SZ_ICON_UP, "^", "up", t.cwd > 0, "Up (Backspace)")) tree_up(t);
    ImGui::SameLine(0, 10);

    float search_w = 240.0f * (float)g_font_px / 18.0f;
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    std::vector<int> crumbs = tree_breadcrumb(t);
    for (size_t i = 0; i < crumbs.size(); ++i) {
        if (i) {
            ImGui::SameLine(0, 0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", g_icons ? SZ_ICON_CHEVRON : ">");
            ImGui::SameLine(0, 0);
        }
        std::string label = crumbs[i] == 0 ? fs::path(a->opened_path).filename().u8string() : t.nodes[crumbs[i]].name;
        want_glyphs(label);
        ImGui::PushID((int)i);
        if (ImGui::Button(label.c_str())) go_to = crumbs[i];
        ImGui::PopID();
    }
    ImGui::PopStyleColor(2);
    ImGui::SameLine();
    right_align(search_w);
    ImGui::SetNextItemWidth(search_w);
    std::string hint = "Search " + (t.cwd == 0 ? fs::path(a->opened_path).filename().u8string() : t.nodes[t.cwd].name);
    ImGui::InputTextWithHint("##search", hint.c_str(), t.search_input, sizeof(t.search_input));
    want_glyphs(t.search_input);
    tree_set_search(t, t.search_input);
    tree_refresh(t);

    const bool show_packed = a->okind == AKind::Satz || a->okind == AKind::Zip;
    const float status_h = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    const ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_BordersOuterH |
                                  ImGuiTableFlags_SizingStretchProp;
    const ImVec4 acc = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
    ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(acc.x, acc.y, acc.z, 0.30f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(acc.x, acc.y, acc.z, 0.14f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(acc.x, acc.y, acc.z, 0.38f));
    if (ImGui::BeginTable("browser", show_packed ? 5 : 4, flags, ImVec2(0, ImGui::GetContentRegionAvail().y - status_h))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_NoHide, 4.0f, (ImGuiID)SortKey::Name);
        ImGui::TableSetupColumn("Date modified", 0, 2.0f, (ImGuiID)SortKey::Modified);
        ImGui::TableSetupColumn("Type", 0, 2.0f, (ImGuiID)SortKey::Type);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_PreferSortDescending, 1.2f, (ImGuiID)SortKey::Size);
        if (show_packed)
            ImGui::TableSetupColumn("Packed", ImGuiTableColumnFlags_PreferSortDescending, 1.2f, (ImGuiID)SortKey::Packed);
        ImGui::TableHeadersRow();
        if (ImGuiTableSortSpecs* ss = ImGui::TableGetSortSpecs()) {
            if (ss->SpecsDirty && ss->SpecsCount > 0) {
                tree_set_sort(t, (SortKey)ss->Specs[0].ColumnUserID, ss->Specs[0].SortDirection != ImGuiSortDirection_Descending);
                ss->SpecsDirty = false;
            }
        }
        tree_refresh(t);

        const std::string strip = tree_strip_prefix(t);
        const ImVec4 folder_col = rgb(0xF2C14E);
        ImGuiListClipper clip;
        clip.Begin((int)t.listing.size());
        while (clip.Step()) {
            for (int k = clip.DisplayStart; k < clip.DisplayEnd; ++k) {
                const int id = t.listing[k];
                const TreeNode& n = t.nodes[id];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(id);
                const bool sel = t.selected[id] != 0;
                if (ImGui::Selectable("##row", sel, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap |
                                                    ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        if (n.is_dir) go_to = id;
                        else if (n.entry >= 0) { act.cmd = BrowserCmd::Open; act.entry = (uint32_t)n.entry; }
                    } else {
                        tree_click(t, k, io.KeyCtrl, io.KeyShift);
                    }
                }
                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && act.cmd == BrowserCmd::None) {
                    if (!sel) tree_click(t, k, false, false);
                    act.cmd = BrowserCmd::DragOut;
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !sel) tree_click(t, k, false, false);
                if (ImGui::BeginPopupContextItem("ctx")) {
                    if (ImGui::MenuItem("Open")) {
                        if (n.is_dir) go_to = id;
                        else if (n.entry >= 0) { act.cmd = BrowserCmd::Open; act.entry = (uint32_t)n.entry; }
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Extract")) act.cmd = BrowserCmd::ExtractHere;
                    if (ImGui::MenuItem("Extract to...")) act.cmd = BrowserCmd::ExtractTo;
                    ImGui::EndPopup();
                }
                ImGui::SameLine();
                if (g_icons) {
                    if (n.is_dir) ImGui::TextColored(folder_col, SZ_ICON_FOLDER);
                    else ImGui::TextDisabled(SZ_ICON_FILE);
                    ImGui::SameLine();
                }
                std::string shown = n.name;
                if (!t.search.empty()) {
                    shown = tree_path(t, id);
                    if (shown.compare(0, strip.size(), strip) == 0) shown = shown.substr(strip.size());
                }
                want_glyphs(shown);
                ImGui::TextUnformatted(shown.c_str());
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", format_local_time(n.mtime).c_str());
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", file_type_name(n.name, n.is_dir).c_str());
                ImGui::TableNextColumn();
                if (!n.is_dir) ImGui::TextUnformatted(fmt_size(n.size).c_str());
                if (show_packed) {
                    ImGui::TableNextColumn();
                    if (!n.is_dir) ImGui::TextDisabled("%s", fmt_size(n.stored).c_str());
                }
                ImGui::PopID();
            }
        }
        if (t.listing.empty()) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled(t.search.empty() ? "This folder is empty." : "No items match your search.");
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleColor(3);

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup)) {
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A)) tree_select_all(t);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) tree_clear_selection(t);
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace) || (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_UpArrow))) tree_up(t);
        if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) tree_back(t);
        if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_RightArrow)) tree_forward(t);
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
            int first = tree_first_selected(t);
            if (first >= 0) {
                if (t.nodes[first].is_dir) go_to = first;
                else if (t.nodes[first].entry >= 0) { act.cmd = BrowserCmd::Open; act.entry = (uint32_t)t.nodes[first].entry; }
            }
        }
    }
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
        if (ImGui::IsMouseClicked(3)) tree_back(t);
        if (ImGui::IsMouseClicked(4)) tree_forward(t);
    }

    size_t count = t.listing.size(), nsel = tree_selected_count(t);
    std::string status = std::to_string(count) + (count == 1 ? " item" : " items");
    if (nsel) {
        status += "      " + std::to_string(nsel) + (nsel == 1 ? " item selected" : " items selected");
        uint64_t bytes = tree_selected_size(t);
        if (bytes) status += "  " + fmt_size(bytes);
    }
    ImGui::TextDisabled("%s", status.c_str());

    if (go_to >= 0) tree_go(t, go_to);
}

static void drag_selection_out(App* a) {
    std::vector<uint32_t> entries;
    std::vector<std::pair<std::string, bool>> tops;
    std::string strip;
    { std::lock_guard<std::mutex> lk(a->mtx);
      const ArchiveTree& t = a->tree;
      entries = tree_selected_entries(t);
      strip = tree_strip_prefix(t);
      for (size_t id = 0; id < t.selected.size(); ++id) {
          if (!t.selected[id]) continue;
          std::string rel = tree_path(t, (int)id);
          if (rel.compare(0, strip.size(), strip) == 0) rel = rel.substr(strip.size());
          tops.push_back({ rel, t.nodes[id].is_dir });
      } }
    if (entries.empty()) return;

    std::error_code ec;
    fs::path root = fs::temp_directory_path(ec) / "SAT-ZIP" / "drag";
    const auto now = fs::file_time_type::clock::now();
    for (const auto& d : fs::directory_iterator(root, ec))
        if (now - d.last_write_time(ec) > std::chrono::hours(1)) fs::remove_all(d.path(), ec);
    fs::path temp = root / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());

    std::vector<fs::path> paths;
    for (const auto& top : tops) {
        fs::path p = temp / fs::path(top.first);
        if (top.second) fs::create_directories(p, ec);
        else { fs::create_directories(p.parent_path(), ec); std::ofstream(p, std::ios::binary); }
        paths.push_back(p);
    }
    a->cancel = false;
    bool dropped = drag_out(paths, [&] { do_extract(a, temp.string(), entries, strip); });

    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    if (!dropped) { fs::remove_all(temp, ec); return; }
    std::lock_guard<std::mutex> lk(a->mtx);
    a->status = "Dragged " + std::to_string(tops.size()) + (tops.size() == 1 ? " item" : " items") + " out of the archive";
}

void run_browser_action(App* a, const BrowserAction& act) {
    if (act.cmd == BrowserCmd::None || a->running) return;
    if (act.cmd == BrowserCmd::Open) {
        launch(a, std::thread(do_open_entry, a, act.entry));
        return;
    }
    if (act.cmd == BrowserCmd::DragOut) {
        drag_selection_out(a);
        return;
    }
    std::vector<uint32_t> entries;
    std::string strip, archive;
    { std::lock_guard<std::mutex> lk(a->mtx);
      entries = tree_selected_entries(a->tree);
      if (!entries.empty()) strip = tree_strip_prefix(a->tree);
      archive = a->opened_path; }
    std::string dest;
    if (act.cmd == BrowserCmd::ExtractTo) {
        if (!dlg_folder(dest)) return;
    } else {
        dest = sz::default_extract_dir(fs::path(archive), false).string();
    }
    launch(a, std::thread(do_extract, a, dest, std::move(entries), strip));
}

}
