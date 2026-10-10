// src/adapters/StarDictAdapter.h
// Layer 2b — StarDict format dictionary adapter.
//
// Native entry: src/native/stardict_cli.cpp
//
// StarDict file layout (v2.4.2):
//   <name>.ifo   — metadata (bookname, wordcount, version)
//   <name>.idx   — binary index: word\0 + 4B BE offset + 4B BE size (sorted)
//   <name>.dict  — raw UTF-8 entries at offset, size bytes each
//
// Adapter scans a directory of extracted StarDict dicts, loads all
// .idx files into memory (sorted word → (offset, size, dict_file)),
// and does binary search for define() lookups.

#pragma once

#include "glue/AbstractAdapter.h"
#include "data/SemanticTypes.h"
#include <string>
#include <vector>
#include <map>

namespace muji {

struct StardictDictInfo {
    std::string dir;         // e.g. "stardict-xdict-ec-gb-2.4.2"
    std::string basename;    // e.g. "xdict-ec-gb"
    std::string bookname;    // e.g. "XDICT英汉辞典"
    size_t wordcount = 0;
    size_t idxsize = 0;
    std::string idx_path;
    std::string dict_path;
    std::string ifo_path;
};

class StarDictAdapter : public AbstractAdapter {
public:
    StarDictAdapter();
    ~StarDictAdapter() override;

    // Root directory containing extracted stardict dirs.
    // Default: "../dic/extracted" relative to CWD.
    void set_dict_dir(std::string path);

    bool init() override;
    bool shutdown() override;

    // List all loaded dictionaries.
    std::vector<StardictDictInfo> list_dictionaries() const;

    // Define a word. db = "*" for all, or specific basename.
    std::vector<DictEntry> define(const std::string& word,
                                  const std::string& db = "*") const;

private:
    std::string dict_dir_;

    // Each loaded dict's index: sorted words → (offset, size, dict_id).
    struct DictIndex {
        StardictDictInfo info;
        // word → (offset_in_dict, size_in_bytes)
        std::map<std::string, std::pair<uint32_t, uint32_t>> entries;
    };
    std::vector<DictIndex> indexes_;

    // Parse .ifo → key=value
    static std::map<std::string, std::string> parse_ifo(const std::string& path);

    // Load one dictionary's .idx into memory. Returns wordcount.
    bool load_dict(const std::string& dir_path);

    // Read .dict file at offset, return raw bytes
    static std::string read_dict_entry(const std::string& dict_path,
                                       uint32_t offset, uint32_t size);
};

} // namespace muji
