#pragma once

#include "app_state.hpp"

namespace szgui {

enum class BrowserCmd { None, Open, ExtractHere, ExtractTo, DragOut };

struct BrowserAction { BrowserCmd cmd = BrowserCmd::None; uint32_t entry = 0; };

void draw_extract_buttons(App* a, BrowserAction& act);
void draw_archive_browser(App* a, BrowserAction& act);
void run_browser_action(App* a, const BrowserAction& act);

}
