#include "archive_tree.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace szgui {

static std::string normalize_entry_path(const std::string& raw) {
    std::string out;
    size_t i = 0;
    while (i < raw.size()) {
        size_t j = i;
        while (j < raw.size() && raw[j] != '/' && raw[j] != '\\') ++j;
        std::string part = raw.substr(i, j - i);
        if (!part.empty() && part != ".") {
            if (!out.empty()) out += '/';
            out += part;
        }
        i = j + 1;
    }
    return out;
}

static int ensure_dir(ArchiveTree& t, std::unordered_map<std::string, int>& dirs, const std::string& path) {
    auto it = dirs.find(path);
    if (it != dirs.end()) return it->second;
    size_t cut = path.rfind('/');
    int parent = cut == std::string::npos ? 0 : ensure_dir(t, dirs, path.substr(0, cut));
    TreeNode n;
    n.name = cut == std::string::npos ? path : path.substr(cut + 1);
    n.is_dir = true;
    n.parent = parent;
    int id = (int)t.nodes.size();
    t.nodes.push_back(std::move(n));
    t.nodes[parent].children.push_back(id);
    dirs.emplace(path, id);
    return id;
}

void tree_build(ArchiveTree& t, const std::vector<ViewEntry>& entries, uint64_t rev) {
    std::string keep = t.nodes.empty() ? std::string() : tree_path(t, t.cwd);
    t.nodes.clear();
    TreeNode root;
    root.is_dir = true;
    t.nodes.push_back(root);
    std::unordered_map<std::string, int> dirs;
    dirs.emplace(std::string(), 0);
    for (size_t i = 0; i < entries.size(); ++i) {
        const ViewEntry& e = entries[i];
        std::string p = normalize_entry_path(e.name);
        if (p.empty()) continue;
        if (e.is_dir) {
            int id = ensure_dir(t, dirs, p);
            t.nodes[id].entry = (int)i;
            t.nodes[id].mtime = e.mtime;
            continue;
        }
        size_t cut = p.rfind('/');
        int parent = cut == std::string::npos ? 0 : ensure_dir(t, dirs, p.substr(0, cut));
        TreeNode n;
        n.name = cut == std::string::npos ? p : p.substr(cut + 1);
        n.size = e.orig;
        n.stored = e.stored;
        n.mtime = e.mtime;
        n.entry = (int)i;
        n.parent = parent;
        t.nodes.push_back(std::move(n));
        t.nodes[parent].children.push_back((int)t.nodes.size() - 1);
    }
    auto found = dirs.find(keep);
    t.cwd = found != dirs.end() ? found->second : 0;
    t.rev = rev;
    t.back.clear();
    t.forward.clear();
    t.selected.assign(t.nodes.size(), 0);
    t.anchor = -1;
    t.dirty = true;
}

static void enter(ArchiveTree& t, int node) {
    t.cwd = node;
    t.selected.assign(t.nodes.size(), 0);
    t.anchor = -1;
    t.search.clear();
    t.search_input[0] = 0;
    t.dirty = true;
}

void tree_go(ArchiveTree& t, int node) {
    if (node < 0 || node >= (int)t.nodes.size() || !t.nodes[node].is_dir || node == t.cwd) return;
    t.back.push_back(t.cwd);
    t.forward.clear();
    enter(t, node);
}

void tree_back(ArchiveTree& t) {
    if (t.back.empty()) return;
    t.forward.push_back(t.cwd);
    int node = t.back.back();
    t.back.pop_back();
    enter(t, node);
}

void tree_forward(ArchiveTree& t) {
    if (t.forward.empty()) return;
    t.back.push_back(t.cwd);
    int node = t.forward.back();
    t.forward.pop_back();
    enter(t, node);
}

void tree_up(ArchiveTree& t) {
    if (t.cwd > 0) tree_go(t, t.nodes[t.cwd].parent);
}

void tree_set_sort(ArchiveTree& t, SortKey key, bool asc) {
    if (t.sort_key == key && t.sort_asc == asc) return;
    t.sort_key = key;
    t.sort_asc = asc;
    t.dirty = true;
}

void tree_set_search(ArchiveTree& t, const std::string& text) {
    if (t.search == text) return;
    t.search = text;
    t.selected.assign(t.nodes.size(), 0);
    t.anchor = -1;
    t.dirty = true;
}

std::string file_extension(const std::string& name) {
    size_t dot = name.rfind('.');
    if (dot == std::string::npos || dot == 0) return std::string();
    std::string ext = name.substr(dot + 1);
    for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
    return ext;
}

static int natural_cmp(const std::string& a, const std::string& b) {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        unsigned char ca = (unsigned char)a[i], cb = (unsigned char)b[j];
        if (std::isdigit(ca) && std::isdigit(cb)) {
            size_t i0 = i, j0 = j;
            while (i0 < a.size() && a[i0] == '0') ++i0;
            while (j0 < b.size() && b[j0] == '0') ++j0;
            size_t ie = i0, je = j0;
            while (ie < a.size() && std::isdigit((unsigned char)a[ie])) ++ie;
            while (je < b.size() && std::isdigit((unsigned char)b[je])) ++je;
            if (ie - i0 != je - j0) return (ie - i0) < (je - j0) ? -1 : 1;
            int c = a.compare(i0, ie - i0, b, j0, je - j0);
            if (c) return c < 0 ? -1 : 1;
            i = ie;
            j = je;
            continue;
        }
        int la = std::tolower(ca), lb = std::tolower(cb);
        if (la != lb) return la < lb ? -1 : 1;
        ++i;
        ++j;
    }
    size_t ra = a.size() - i, rb = b.size() - j;
    return ra == rb ? 0 : (ra < rb ? -1 : 1);
}

void tree_refresh(ArchiveTree& t) {
    if (!t.dirty) return;
    t.listing.clear();
    if (t.search.empty()) {
        t.listing = t.nodes[t.cwd].children;
    } else {
        std::string needle = t.search;
        for (auto& c : needle) c = (char)std::tolower((unsigned char)c);
        std::vector<int> stack(t.nodes[t.cwd].children.rbegin(), t.nodes[t.cwd].children.rend());
        while (!stack.empty()) {
            int id = stack.back();
            stack.pop_back();
            std::string name = t.nodes[id].name;
            for (auto& c : name) c = (char)std::tolower((unsigned char)c);
            if (name.find(needle) != std::string::npos) t.listing.push_back(id);
            const auto& ch = t.nodes[id].children;
            for (auto it = ch.rbegin(); it != ch.rend(); ++it) stack.push_back(*it);
        }
    }
    const SortKey key = t.sort_key;
    const bool asc = t.sort_asc;
    std::stable_sort(t.listing.begin(), t.listing.end(), [&](int x, int y) {
        const TreeNode& a = t.nodes[x];
        const TreeNode& b = t.nodes[y];
        int c = 0;
        if (a.is_dir != b.is_dir) c = a.is_dir ? -1 : 1;
        else {
            switch (key) {
            case SortKey::Modified: c = a.mtime == b.mtime ? 0 : (a.mtime < b.mtime ? -1 : 1); break;
            case SortKey::Type:     c = natural_cmp(file_extension(a.name), file_extension(b.name)); break;
            case SortKey::Size:     c = a.size == b.size ? 0 : (a.size < b.size ? -1 : 1); break;
            case SortKey::Packed:   c = a.stored == b.stored ? 0 : (a.stored < b.stored ? -1 : 1); break;
            case SortKey::Name:     break;
            }
            if (c == 0) c = natural_cmp(a.name, b.name);
        }
        return asc ? c < 0 : c > 0;
    });
    t.anchor = -1;
    t.dirty = false;
}

std::string tree_path(const ArchiveTree& t, int node) {
    std::string path;
    while (node > 0) {
        path = path.empty() ? t.nodes[node].name : t.nodes[node].name + "/" + path;
        node = t.nodes[node].parent;
    }
    return path;
}

std::vector<int> tree_breadcrumb(const ArchiveTree& t) {
    std::vector<int> crumbs;
    for (int n = t.cwd; n > 0; n = t.nodes[n].parent) crumbs.push_back(n);
    crumbs.push_back(0);
    std::reverse(crumbs.begin(), crumbs.end());
    return crumbs;
}

void tree_click(ArchiveTree& t, int pos, bool ctrl, bool shift) {
    if (pos < 0 || pos >= (int)t.listing.size()) return;
    int node = t.listing[pos];
    if (shift && t.anchor >= 0 && t.anchor < (int)t.listing.size()) {
        if (!ctrl) t.selected.assign(t.nodes.size(), 0);
        int lo = std::min(t.anchor, pos), hi = std::max(t.anchor, pos);
        for (int k = lo; k <= hi; ++k) t.selected[t.listing[k]] = 1;
        return;
    }
    if (ctrl) t.selected[node] = !t.selected[node];
    else {
        t.selected.assign(t.nodes.size(), 0);
        t.selected[node] = 1;
    }
    t.anchor = pos;
}

void tree_select_all(ArchiveTree& t) {
    for (int id : t.listing) t.selected[id] = 1;
}

void tree_clear_selection(ArchiveTree& t) {
    t.selected.assign(t.nodes.size(), 0);
    t.anchor = -1;
}

size_t tree_selected_count(const ArchiveTree& t) {
    size_t n = 0;
    for (int id : t.listing) if (t.selected[id]) ++n;
    return n;
}

uint64_t tree_selected_size(const ArchiveTree& t) {
    uint64_t total = 0;
    for (int id : t.listing) if (t.selected[id] && !t.nodes[id].is_dir) total += t.nodes[id].size;
    return total;
}

int tree_first_selected(const ArchiveTree& t) {
    for (int id : t.listing) if (t.selected[id]) return id;
    return -1;
}

std::vector<uint32_t> tree_selected_entries(const ArchiveTree& t) {
    std::vector<uint32_t> out;
    std::vector<int> stack;
    for (int id : t.listing) if (t.selected[id]) stack.push_back(id);
    while (!stack.empty()) {
        int id = stack.back();
        stack.pop_back();
        if (t.nodes[id].entry >= 0) out.push_back((uint32_t)t.nodes[id].entry);
        for (int c : t.nodes[id].children) stack.push_back(c);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::string tree_strip_prefix(const ArchiveTree& t) {
    std::string p = tree_path(t, t.cwd);
    return p.empty() ? p : p + "/";
}

}
