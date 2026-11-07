#pragma once

#include <climits>
#include <cstdint>
#include <string>
#include <vector>

namespace szgui {

const int64_t kNoTime = INT64_MIN;

struct ViewEntry { std::string name; bool is_dir; uint64_t orig, stored; uint32_t idx; int64_t mtime; };

struct TreeNode {
    std::string name;
    bool is_dir = false;
    uint64_t size = 0, stored = 0;
    int64_t mtime = kNoTime;
    int entry = -1;
    int parent = -1;
    std::vector<int> children;
};

enum class SortKey { Name, Modified, Type, Size, Packed };

struct ArchiveTree {
    std::vector<TreeNode> nodes;
    uint64_t rev = UINT64_MAX;
    int cwd = 0;
    std::vector<int> back, forward;
    std::vector<int> listing;
    std::vector<char> selected;
    int anchor = -1;
    SortKey sort_key = SortKey::Name;
    bool sort_asc = true;
    std::string search;
    char search_input[256] = {0};
    bool dirty = true;
};

void tree_build(ArchiveTree& t, const std::vector<ViewEntry>& entries, uint64_t rev);
void tree_go(ArchiveTree& t, int node);
void tree_back(ArchiveTree& t);
void tree_forward(ArchiveTree& t);
void tree_up(ArchiveTree& t);
void tree_set_sort(ArchiveTree& t, SortKey key, bool asc);
void tree_set_search(ArchiveTree& t, const std::string& text);
void tree_refresh(ArchiveTree& t);
std::string tree_path(const ArchiveTree& t, int node);
std::vector<int> tree_breadcrumb(const ArchiveTree& t);
void tree_click(ArchiveTree& t, int pos, bool ctrl, bool shift);
void tree_select_all(ArchiveTree& t);
void tree_clear_selection(ArchiveTree& t);
size_t tree_selected_count(const ArchiveTree& t);
uint64_t tree_selected_size(const ArchiveTree& t);
int tree_first_selected(const ArchiveTree& t);
std::vector<uint32_t> tree_selected_entries(const ArchiveTree& t);
std::string tree_strip_prefix(const ArchiveTree& t);
std::string file_extension(const std::string& name);

}
