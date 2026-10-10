// src/native/stardict_cli.cpp
// === NATIVE ENTRY POINT for StarDict (layer 2b) ===
//
// Standalone CLI for StarDict .ifo/.idx/.dict dictionaries.
// Completely independent of MUJI glue — wraps the same StarDict
// binary format directly.
//
// Compile (via CMake or direct):
//   g++ -std=c++17 src/native/stardict_cli.cpp -o build/bin/stardict_cli
//
// Examples:
//   stardict_cli list                       # list all dicts in dic/extracted/
//   stardict_cli define memory              # search all dicts for "memory"
//   stardict_cli define memory -d CDICT5    # search specific dict

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

static void usage(const char* p) {
    std::fprintf(stderr,
        "Usage: %s <cmd> [args]\n"
        "  list                 list all loaded StarDict dictionaries\n"
        "  define <word>        look up word (all dicts)\n"
        "  define <word> -d <d> look up word in specific dict\n"
        "  -D <dir>             dict root dir [../dic/extracted]\n"
        "  -h                   show this help\n", p);
}

struct DictEntry {
    std::string word;
    std::string database;   // basename
    std::string db_name;    // bookname
    std::string definition;
};

struct DictInfo {
    std::string dir;
    std::string basename;
    std::string bookname;
    size_t wordcount = 0;
    std::string idx_path;
    std::string dict_path;
};

struct LoadedDict {
    DictInfo info;
    // word → (offset, size)
    std::map<std::string, std::pair<uint32_t, uint32_t>> entries;
};

static std::map<std::string, std::string> parse_ifo(const std::string& p) {
    std::map<std::string, std::string> kv;
    std::ifstream f(p);
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq != std::string::npos) kv[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return kv;
}

static bool load_one_dict(const std::string& dir, LoadedDict& out) {
    std::string ifo_path, idx_path, dict_path;
    DIR* d = opendir(dir.c_str());
    if (!d) { std::fprintf(stderr, "OPEN DIR FAIL: %s (%s)\n", dir.c_str(), strerror(errno)); return false; }
    struct dirent* ent;
    while ((ent = readdir(d))) {
        std::string n = ent->d_name;
        if (n.size() < 5) continue;
        std::string ext = n.substr(n.rfind('.'));
        if (ext == ".ifo") ifo_path = dir + "/" + n;
        else if (ext == ".idx") idx_path = dir + "/" + n;
        else if (ext == ".dict") dict_path = dir + "/" + n;
    }
    closedir(d);
    if (ifo_path.empty() || idx_path.empty() || dict_path.empty()) {
        std::fprintf(stderr, "MISSING in %s: ifo=%d idx=%d dict=%d\n",
                     dir.c_str(), !ifo_path.empty(), !idx_path.empty(), !dict_path.empty());
        return false;
    }

    auto kv = parse_ifo(ifo_path);
    if (kv.find("version") == kv.end()) {
        std::fprintf(stderr, "NO VERSION in %s (keys: ", ifo_path.c_str());
        for (auto& p : kv) std::fprintf(stderr, "%s,", p.first.c_str());
        std::fprintf(stderr, ")\n");
        return false;
    }

    out.info.dir = dir;
    out.info.basename = kv.count("bookname") ? kv["bookname"] : "";
    if (kv.count("wordcount")) out.info.wordcount = std::stoull(kv["wordcount"]);
    out.info.idx_path = idx_path;
    out.info.dict_path = dict_path;

    // Also get file basename
    auto slash = ifo_path.find_last_of('/');
    std::string fname = (slash == std::string::npos) ? ifo_path : ifo_path.substr(slash + 1);
    std::string base = fname.substr(0, fname.rfind(".ifo"));

    // Load idx
    std::ifstream idx(idx_path, std::ios::binary);
    if (!idx) return false;
    std::ostringstream ss; ss << idx.rdbuf();
    std::string data = ss.str();

    uint8_t* p = (uint8_t*)data.data();
    size_t n = data.size();
    size_t i = 0;
    while (i < n) {
        size_t j = i;
        while (j < n && p[j] != 0) ++j;
        if (j >= n) break;
        std::string word((char*)(p + i), j - i);
        i = j + 1;
        if (i + 8 > n) break;
        uint32_t off = (uint32_t(p[i]) << 24) | (uint32_t(p[i+1]) << 16) |
                       (uint32_t(p[i+2]) << 8)  | uint32_t(p[i+3]);
        uint32_t sz  = (uint32_t(p[i+4]) << 24) | (uint32_t(p[i+5]) << 16) |
                       (uint32_t(p[i+6]) << 8)  | uint32_t(p[i+7]);
        i += 8;
        out.entries[word] = {off, sz};
    }
    return true;
}

static std::string read_dict_at(const std::string& dict_path,
                                uint32_t off, uint32_t sz) {
    std::ifstream f(dict_path, std::ios::binary);
    if (!f) return {};
    f.seekg(off);
    std::string out(sz, '\0');
    f.read(&out[0], sz);
    while (!out.empty() && out.back() == 0) out.pop_back();
    return out;
}

int main(int argc, char** argv) {
    if (argc < 2) { usage(argv[0]); return 1; }

    std::string cmd, word, dict_dir = "../dic/extracted";
    std::string db_filter = "*";

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(argv[0]); return 0; }
        else if (!strcmp(a, "-D") && i+1 < argc) dict_dir = argv[++i];
        else if (!strcmp(a, "-d") && i+1 < argc) db_filter = argv[++i];
        else if (!strcmp(a, "list") || !strcmp(a, "define")) cmd = a;
        else if (cmd.empty()) { usage(argv[0]); return 1; }
        else if (word.empty()) word = a;
    }

    std::fprintf(stderr, "DEBUG: cmd=%s dict_dir=%s db_filter=%s\n",
                 cmd.c_str(), dict_dir.c_str(), db_filter.c_str());

    std::vector<LoadedDict> dicts;
    DIR* d = opendir(dict_dir.c_str());
    if (!d) {
        std::fprintf(stderr, "Error: cannot open dict dir %s (%s)\n", dict_dir.c_str(), strerror(errno));
        return 2;
    }
    struct dirent* ent;
    int dir_count = 0;
    while ((ent = readdir(d))) {
        std::string sub = ent->d_name;
        if (sub[0] == '.') continue;
        std::string full = dict_dir + "/" + sub;
        struct stat st;
        if (stat(full.c_str(), &st) != 0) continue;
        if (!S_ISDIR(st.st_mode)) continue;
        ++dir_count;
        std::fprintf(stderr, "  loading %s...\n", sub.c_str());
        LoadedDict ld;
        if (load_one_dict(full, ld)) {
            std::fprintf(stderr, "    OK: %s (%zu words, %zu entries)\n",
                         ld.info.basename.c_str(), ld.info.wordcount, ld.entries.size());
            dicts.push_back(std::move(ld));
        } else {
            std::fprintf(stderr, "    FAIL\n");
        }
    }
    closedir(d);
    std::fprintf(stderr, "DEBUG: %d subdirs found, %zu dicts loaded\n", dir_count, dicts.size());

    if (cmd == "list") {
        std::printf("%zu dictionaries loaded:\n\n", dicts.size());
        for (auto& d : dicts) {
            std::printf("  %-40s  %8zu words   (%s)\n",
                        d.info.basename.c_str(), d.info.wordcount, d.info.dir.c_str());
        }
        return 0;
    }

    if (cmd == "define") {
        if (word.empty()) { usage(argv[0]); return 1; }
        int found = 0;
        for (auto& d : dicts) {
            if (db_filter != "*" && d.info.basename != db_filter) continue;
            auto it = d.entries.find(word);
            if (it != d.entries.end()) {
                ++found;
                std::printf("═══ [%s] %s (%zu words) ═══\n",
                            d.info.basename.c_str(), d.info.dir.c_str(), d.info.wordcount);
                std::printf("  %s\n\n", read_dict_at(d.info.dict_path, it->second.first, it->second.second).c_str());
            }
        }
        if (found == 0) {
            std::printf("(not found in any dictionary)\n");
            return 1;
        }
        return 0;
    }

    usage(argv[0]);
    return 1;
}
