#pragma once
#include "glue/AbstractAdapter.h"
#include "data/SemanticTypes.h"
#include <string>
#include <vector>
#include <memory>
#include <zim/archive.h>

namespace muji {

/// Adapter for ZIM file reading (offline Wikipedia / Wiktionary etc.)
/// Wraps libzim directly — no Qt, no kiwix-desktop GUI.
/// Native entry: build/bin/kiwix_cli
class KiwixAdapter : public AbstractAdapter {
public:
    KiwixAdapter() : AbstractAdapter("Kiwix") {}
    ~KiwixAdapter() override { shutdown(); }

    bool init() override { return true; }
    bool shutdown() override;

    /// Scan dir and auto-open all .zim files
    bool load_directory(const std::string& dir);

    /// Open a specific .zim file
    bool open_file(const std::string& path);

    // -- Query interface --

    struct ZimEntry {
        std::string zim_name;
        std::string title;
        std::string path;
        std::string content;      // HTML or text (capped)
        std::string mimetype;
        zim::size_type size = 0;
        bool is_redirect = false;
    };

    struct ZimInfo {
        std::string filename;
        std::string human_name;
        std::string description;
        std::string language;
        zim::entry_index_type entry_count = 0;
        zim::entry_index_type article_count = 0;
    };

    std::vector<ZimInfo> list_archives() const;
    std::vector<ZimEntry> get_by_title(const std::string& title) const;
    std::vector<ZimEntry> get_by_path(const std::string& path) const;

private:
    std::string zim_dir_;
    struct OpenArchive {
        std::string path;
        std::unique_ptr<zim::Archive> arch;
    };
    std::vector<OpenArchive> archives_;

    ZimEntry extract_entry(const std::string& zim_name, const zim::Entry& e) const;
};

} // namespace muji
