#pragma once

#include "app_state.hpp"

namespace szgui {

void run_benchmark(App* a);
void load_bench_file(App* a, const std::string& path);
void do_load_bench_folder(App* a, std::string dir);
void gen_corpus(App* a, const char* kind);

}
