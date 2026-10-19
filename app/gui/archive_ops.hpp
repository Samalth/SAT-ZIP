#pragma once

#include "app_state.hpp"

namespace szgui {

void stage_paths(App* a, const std::vector<std::string>& paths);
void do_create(App* a, std::string outpath);
void do_extract(App* a, std::string destdir, bool selected_only);
void do_open_entry(App* a, uint32_t vidx);
void do_edit(App* a, std::vector<std::string> add_paths, bool remove_selected);

}
