#pragma once

#include "app_state.hpp"

namespace szgui {

void stage_paths(App* a, const std::vector<std::string>& paths);
void open_path(App* a, const std::string& path);
void do_create(App* a, std::string outpath);
void do_extract(App* a, std::string destdir, std::vector<uint32_t> entries, std::string strip);
void do_open_entry(App* a, uint32_t vidx);
void do_edit(App* a, std::vector<std::string> add_paths, std::vector<uint32_t> remove_entries);

}
