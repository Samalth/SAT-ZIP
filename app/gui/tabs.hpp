#pragma once

#include "app_state.hpp"

namespace szgui {

void draw_archive_tab(App* a);
void draw_benchmark_tab(App* a);
void draw_settings_tab(App* a);
void draw_info_tab(App* a);
bool settings_dirty(const App* a);
void save_settings_now(App* a);

}
