// src/adapters/StarDictAdapter.cpp
// StarDict binary format adapter. Pure C++, no external deps.

#include "adapters/StarDictAdapter.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace muji {

StarDictAdapter::StarDictAdapter() : AbstractAdapter("StarDict") {
    // Default: <project_root>/dic/extracted
    dict_dir_ = "../dic/extracted";
}

StarDictAdapter::~StarDictAdapter() { shutdown(); }

void StarDictAdapter::set_dict_dir(std::string path) {
    dict_dir_ = std::move(path);
}

bool StarDictAdapter::shutdown() {
    indexes_.clear();
    set_status(AdapterStatus::Shutdown);
    return true;
}

std::map<std::string, std::string> StarDictAdapter::parse_ifo(const std::string& path) {
    std::map<std::string, std::string> kv;
    std::ifstream f(path);
    if (!f) return kv;
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        std::string v = line.substr(eq + 1);
        kv[k] = v;
    }
    return kv;
}

bool StarDictAdapter::load_dict(const std::string& dir_path) {
    // Find .ifo file
    std::string ifo_path;
    std::string idx_path;
    std::string dict_path;

    // List dir contents (simple glob)
    DIR* d = opendir(dir_path.c_str());
    if (!d) return false;
    struct dirent* ent;
    while ((ent = readdir(d))) {
        std::string name = ent->d_name;
        if (name.size() < 5) continue;
        std::string ext = name.substr(name.rfind('.'));
        if (ext == ".ifo") ifo_path = dir_path + "/" + name;
        else if (ext == ".idx") idx_path = dir_path + "/" + name;
        else if (ext == ".dict") dict_path = dir_path + "/" + name;
        // Also accept .dict.dz (we don't decompress here — user must dictunzip first)
        else if (ext == ".dz" && name.find(".dict") != std::string::npos) {
            // Prefer uncompressed
        }
    }
    closedir(d);

    if (ifo_path.empty() || idx_path.empty() || dict_path.empty()) {
        return false;
    }

    // Parse ifo
    auto meta = parse_ifo(ifo_path);
    if (meta.find("version") == meta.end()) return false;

    DictIndex di;
    di.info.ifo_path = ifo_path;
    di.info.idx_path  = idx_path;
    di.info.dict_path = dict_path;
    di.info.dir = dir_path;
    di.info.basename = meta.count("bookname") ? meta["bookname"] : "";
    if (meta.count("wordcount")) di.info.wordcount = std::stoull(meta["wordcount"]);

    // Get dict filename base (without .ifo extension) → use for "basename" key
    auto slash = ifo_path.find_last_of('/');
    std::string fname = (slash == std::string::npos) ? ifo_path : ifo_path.substr(slash + 1);
    di.info.basename = fname.substr(0, fname.rfind(".ifo"));

    // Load idx into memory
    std::ifstream idx_f(idx_path, std::ios::binary);
    if (!idx_f) return false;
    std::ostringstream ss;
    ss << idx_f.rdbuf();
    std::string idx_data = ss.str();
    di.info.idxsize = idx_data.size();

    uint8_t* p = (uint8_t*)idx_data.data();
    size_t n = idx_data.size();
    size_t i = 0;
    while (i < n) {
        // Word: null-terminated
        size_t j = i;
        while (j < n && p[j] != 0) ++j;
        if (j >= n) break;
        std::string word((char*)(p + i), j - i);
        i = j + 1;  // skip null

        if (i + 8 > n) break;
        uint32_t offset = (uint32_t(p[i]) << 24) | (uint32_t(p[i+1]) << 16) |
                          (uint32_t(p[i+2]) << 8)  | uint32_t(p[i+3]);
        uint32_t size   = (uint32_t(p[i+4]) << 24) | (uint32_t(p[i+5]) << 16) |
                          (uint32_t(p[i+6]) << 8)  | uint32_t(p[i+7]);
        i += 8;

        di.entries[word] = {offset, size};
    }

    indexes_.push_back(std::move(di));
    return true;
}

bool StarDictAdapter::init() {
    indexes_.clear();

    DIR* d = opendir(dict_dir_.c_str());
    if (!d) {
        set_last_error("dict dir not found: " + dict_dir_);
        return false;
    }

    int loaded = 0;
    struct dirent* ent;
    while ((ent = readdir(d))) {
        std::string sub = ent->d_name;
        if (sub[0] == '.') continue;
        std::string full = dict_dir_ + "/" + sub;
        struct stat st;
        if (stat(full.c_str(), &st) != 0) continue;
        if (!S_ISDIR(st.st_mode)) continue;

        if (load_dict(full)) ++loaded;
    }
    closedir(d);

    if (loaded == 0) {
        set_last_error("no stardict dicts found in " + dict_dir_);
        return false;
    }

    set_status(AdapterStatus::Ready);
    return true;
}

std::vector<StardictDictInfo> StarDictAdapter::list_dictionaries() const {
    std::vector<StardictDictInfo> out;
    for (auto& d : indexes_) out.push_back(d.info);
    return out;
}

std::string StarDictAdapter::read_dict_entry(const std::string& dict_path,
                                              uint32_t offset, uint32_t size) {
    std::ifstream f(dict_path, std::ios::binary);
    if (!f) return {};
    f.seekg(offset);
    std::string out(size, '\0');
    f.read(&out[0], size);
    // Trim trailing nulls (StarDict entries are terminated with \0)
    while (!out.empty() && out.back() == 0) out.pop_back();
    return out;
}

std::vector<DictEntry> StarDictAdapter::define(const std::string& word,
                                                 const std::string& db) const {
    std::vector<DictEntry> results;
    std::string w = word;

    for (auto& di : indexes_) {
        // Filter by db: "*" = all, otherwise match basename or bookname
        if (!db.empty() && db != "*") {
            if (di.info.basename != db && di.info.bookname != db) continue;
        }

        auto it = di.entries.find(w);
        if (it != di.entries.end()) {
            DictEntry e;
            e.database  = di.info.basename;
            e.db_name   = di.info.bookname;
            e.word      = w;
            e.definition = read_dict_entry(di.info.dict_path, it->second.first, it->second.second);
            e.source     = SourceTag::StarDict;
            results.push_back(std::move(e));
        }
    }
    return results;
}

} // namespace muji
