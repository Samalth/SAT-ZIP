#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  #include <windows.h>
#endif

#include "tabs.hpp"
#include "platform.hpp"
#include "satzip.h"
#include "satzip_version.h"

#include "imgui.h"

namespace szgui {

void draw_info_tab(App* a) {
    ImGui::Dummy(ImVec2(0, 10));
    ImGui::Text("SAT-ZIP  v%s", SATZIP_VERSION);
    ImGui::TextDisabled("High-throughput file archiver");
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 6));

    ImGui::TextUnformatted("License: Apache-2.0");
    ImGui::TextDisabled("Copyright 2026 Sam Alex Thiers");
    if (ImGui::Button("View full license", ImVec2(170, 0))) open_url("https://www.apache.org/licenses/LICENSE-2.0");
    ImGui::Dummy(ImVec2(0, 10));

    ImGui::TextUnformatted("Latest version & downloads:");
    if (ImGui::Button("Open " SATZIP_REPO_URL)) open_url(SATZIP_REPO_URL);
    ImGui::SameLine();
    if (ImGui::Button("Releases", ImVec2(110, 0))) open_url(SATZIP_REPO_URL "/releases");
    ImGui::Dummy(ImVec2(0, 10));

    if (ImGui::CollapsingHeader("FAQ")) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Why is an archive sometimes barely smaller, or even slightly larger?");
        ImGui::TextUnformatted("Already-compressed or encrypted files (.zip, .7z, .jpg, .mp4, .obl, ...) "
            "have almost no redundancy left, so no tool can shrink them - not SAT-ZIP, 7-Zip or Windows. "
            "SAT-ZIP stores them as-is and adds a little container bookkeeping, which can make the archive "
            "a touch larger than the original.");
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::TextDisabled("Does compressing an image (or any file) reduce its quality?");
        ImGui::TextUnformatted("No. Compression is lossless: the extracted file is byte-for-byte identical "
            "to the original. It only changes size, never content.");
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::TextDisabled("Why is the ratio lower than 7-Zip or Windows zip?");
        ImGui::TextUnformatted("The default modes optimize for speed, especially fast extraction. For the "
            "tightest packing, switch Settings > mode to 'Max ratio (Mode T)': it adds an ANS "
            "entropy-coding stage, trading compression/extraction speed for size.");
        ImGui::PopTextWrapPos();
    }
    ImGui::Dummy(ImVec2(0, 10));

    ImGui::Separator();
    if (!a->obelisk.empty())
        ImGui::TextDisabled("SAT-OBELISK found: %s", a->obelisk.string().c_str());
    else
        ImGui::TextDisabled("SAT-OBELISK not found (install it to enable encrypt-after-create).");
    ImGui::TextDisabled("Codec CPU path: %s", sz_cpu_path());
    ImGui::TextDisabled("Built with Dear ImGui, ImPlot and GLFW; LZ4 used for comparison.");
}

}
