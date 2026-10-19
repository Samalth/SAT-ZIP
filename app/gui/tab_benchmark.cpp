#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  #include <windows.h>
#endif

#include "tabs.hpp"
#include "benchmark.hpp"
#include "fonts.hpp"
#include "platform.hpp"
#include "theme.hpp"
#include "widgets.hpp"

#include "imgui.h"
#include "implot.h"

namespace szgui {

static void plot_phase(App* a, const char* title, int phase) {
    if (ImPlot::BeginPlot(title, ImVec2(-1, 220))) {
        ImPlot::SetupAxes("block", "MB/s", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        std::lock_guard<std::mutex> lk(a->mtx);
        auto line = [&](const char* nm, const Series& s) {
            if (!s.x.empty()) ImPlot::PlotLine(nm, s.x.data(), s.y.data(), (int)s.x.size());
        };
        if (a->show_s)    line("SAT-ZIP S", phase == 0 ? a->sat_s.comp : a->sat_s.decomp);
        if (a->show_r)    line("SAT-ZIP R", phase == 0 ? a->sat_r.comp : a->sat_r.decomp);
        if (a->show_t)    line("SAT-ZIP T", phase == 0 ? a->sat_t.comp : a->sat_t.decomp);
        if (a->show_lz4)  line("LZ4",       phase == 0 ? a->lz4.comp   : a->lz4.decomp);
        if (a->show_zip)  line("zip",       phase == 0 ? a->zipdef.comp : a->zipdef.decomp);
        if (a->show_lzma) line("LZMA",      phase == 0 ? a->lzma.comp  : a->lzma.decomp);
        ImPlot::EndPlot();
    }
}
static void bench_row(const CodecView& c) {
    ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(c.name);
    if (!c.done) { for (int k = 0; k < 5; ++k) { ImGui::TableNextColumn(); ImGui::TextDisabled("--"); } return; }
    ImGui::TableNextColumn(); ImGui::Text("x%.2f", c.result.ratio);
    ImGui::TableNextColumn(); ImGui::Text("%.2f MB", (double)c.result.comp_bytes / 1e6);
    ImGui::TableNextColumn(); ImGui::Text("%.0f", c.result.comp_mbs);
    ImGui::TableNextColumn(); ImGui::Text("%.0f", c.result.decomp_mbs);
    ImGui::TableNextColumn();
    if (c.result.ok) ImGui::TextColored(ImVec4(0.3f,0.9f,0.3f,1), "OK");
    else             ImGui::TextColored(ImVec4(0.95f,0.3f,0.3f,1), "FAIL");
}
void draw_benchmark_tab(App* a) {
    bool busy = a->running;
    ImGui::Text("Source: %s", a->bench_source.c_str());
    if (busy) ImGui::BeginDisabled();
    if (ImGui::Button("Open file...")) { std::string p; if (dlg_open(p, false)) load_bench_file(a, p); }
    ImGui::SetItemTooltip("Benchmark a single file");
    ImGui::SameLine();
    if (ImGui::Button("Open folder...")) { std::string p; if (dlg_folder(p)) launch(a, std::thread(do_load_bench_folder, a, p)); }
    ImGui::SetItemTooltip("Benchmark all files in a folder (concatenated, up to 512 MB)");
    ImGui::SameLine();
    if (ImGui::Button("Synthetic...")) ImGui::OpenPopup("synth");
    ImGui::SetItemTooltip("Generate a 16 MB test corpus");
    if (ImGui::BeginPopup("synth")) {
        if (ImGui::MenuItem("text"))   gen_corpus(a, "text");
        if (ImGui::MenuItem("mixed"))  gen_corpus(a, "mixed");
        if (ImGui::MenuItem("zeros"))  gen_corpus(a, "zeros");
        if (ImGui::MenuItem("random")) gen_corpus(a, "random");
        ImGui::EndPopup();
    }
    wrap_same_line(200); ImGui::SetNextItemWidth(110); ImGui::SliderInt("block KiB", &a->block_kib, 4, 64);
    ImGui::SetItemTooltip("Block size used per compression call");
    wrap_same_line(170); ImGui::SetNextItemWidth(110); ImGui::SliderInt("runs", &a->runs, 1, 50);
    ImGui::SetItemTooltip("Best-of-N timed passes");
    wrap_same_line(170); ImGui::SetNextItemWidth(110); ImGui::SliderInt("level", &a->bench_level, 1, 9);
    ImGui::SetItemTooltip("Compression level for SAT-ZIP / zip / LZMA");
    wrap_same_line(180); ImGui::SetNextItemWidth(110);
    ImGui::SliderInt("threads", &a->bench_threads, 0, 32, a->bench_threads == 0 ? "auto" : "%d");
    ImGui::SetItemTooltip("Worker threads for the aggregate numbers (0 = all cores)");

    {
        const float fw = g_icons ? 44.0f : 90.0f, bw = 150.0f;
        ImGui::SameLine();
        float target = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - fw - bw - ImGui::GetStyle().ItemSpacing.x;
        if (ImGui::GetContentRegionAvail().x < fw + bw + ImGui::GetStyle().ItemSpacing.x) ImGui::NewLine();
        else if (target > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(target);
        if (ImGui::Button(g_icons ? SZ_ICON_FILTER "##filter" : "Filter...", ImVec2(fw, 0))) ImGui::OpenPopup("benchfilter");
        ImGui::SetItemTooltip("Choose which codecs to show (and run)");
        if (ImGui::BeginPopup("benchfilter")) {
            ImGui::Checkbox("SAT-ZIP S", &a->show_s);
            ImGui::Checkbox("SAT-ZIP R", &a->show_r);
            ImGui::Checkbox("SAT-ZIP T", &a->show_t);
            ImGui::Checkbox("LZ4", &a->show_lz4);
            ImGui::Checkbox("zip (deflate)", &a->show_zip);
            ImGui::Checkbox("LZMA", &a->show_lzma);
            ImGui::Separator();
            ImGui::Checkbox("don't run hidden codecs", &a->skip_hidden);
            ImGui::SetItemTooltip("Applies to the next benchmark run; already-computed results stay visible when re-shown");
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        bool can = a->bench_data.size() > SZ_MARGIN;
        if (!can) ImGui::BeginDisabled();
        push_primary_button();
        if (g_font_bold) ImGui::PushFont(g_font_bold);
        bool go = ImGui::Button("Benchmark", ImVec2(bw, 0));
        if (g_font_bold) ImGui::PopFont();
        ImGui::PopStyleColor(5);
        ImGui::SetItemTooltip("Run the benchmark on the loaded source");
        if (go) launch(a, std::thread(run_benchmark, a));
        if (!can) ImGui::EndDisabled();
    }
    if (busy) ImGui::EndDisabled();
    ImGui::TextDisabled("Table = aggregate at the chosen thread count; charts show per-block speed (single thread only).");
    size_t bn = a->bench_data.size() > SZ_MARGIN ? a->bench_data.size() - SZ_MARGIN : 0;
    if (bn && bn < 4u * 1024 * 1024)
        ImGui::TextColored(ImVec4(1, 0.75f, 0.2f, 1),
            "Source is small (%.2f MB): timings are dominated by overhead, not codec speed. Use >= 4 MB.", bn / 1e6);
    if (a->sat_r.done && a->lz4.done && a->sat_r.result.ratio < 1.05 && a->lz4.result.ratio < 1.05)
        ImGui::TextColored(ImVec4(1, 0.75f, 0.2f, 1),
            "Source is incompressible (already compressed/encrypted?): no codec can gain here. Try 'text' or a document/source file.");

    if (ImGui::BeginTable("bench", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("codec"); ImGui::TableSetupColumn("ratio"); ImGui::TableSetupColumn("result");
        ImGui::TableSetupColumn("comp MB/s"); ImGui::TableSetupColumn("decomp MB/s");
        ImGui::TableSetupColumn("status"); ImGui::TableHeadersRow();
        if (a->show_s)    bench_row(a->sat_s);
        if (a->show_r)    bench_row(a->sat_r);
        if (a->show_t)    bench_row(a->sat_t);
        if (a->show_lz4)  bench_row(a->lz4);
        if (a->show_zip)  bench_row(a->zipdef);
        if (a->show_lzma) bench_row(a->lzma);
        ImGui::EndTable();
    }
    plot_phase(a, "Compress throughput", 0);
    plot_phase(a, "Decompress throughput", 1);
}

}
