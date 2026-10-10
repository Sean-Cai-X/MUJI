#include "adapters/KiwixAdapter.h"
#include <zim/entry.h>
#include <zim/item.h>
#include <dirent.h>
#include <sys/stat.h>
#include <iostream>
#include <algorithm>
#include <cstring>

namespace muji {

bool KiwixAdapter::open_file(const std::string& path) {
    try {
        auto arch = std::make_unique<zim::Archive>(path);
        OpenArchive oa;
        oa.path = path;
        oa.arch = std::move(arch);
        archives_.push_back(std::move(oa));
        status_ = AdapterStatus::Ready;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Kiwix] open_file failed for " << path << ": " << e.what() << "\n";
        return false;
    }
}

bool KiwixAdapter::shutdown() {
    archives_.clear();
    set_status(AdapterStatus::Shutdown);
    return true;
}

bool KiwixAdapter::load_directory(const std::string& dir) {
    zim_dir_ = dir;
    shutdown();
    DIR* d = opendir(dir.c_str());
    if (!d) {
        std::cerr << "[Kiwix] cannot open ZIM dir: " << dir << "\n";
        return false;
    }
    struct dirent* ent;
    int loaded = 0;
    while ((ent = readdir(d)) != nullptr) {
        std::string name = ent->d_name;
        if (name.size() < 5 || name.substr(name.size()-4) != ".zim") continue;
        std::string full = dir + "/" + name;
        if (open_file(full)) loaded++;
    }
    closedir(d);
    std::cerr << "[Kiwix] loaded " << loaded << " ZIM archives from " << dir << "\n";
    return loaded > 0;
}

std::vector<KiwixAdapter::ZimInfo> KiwixAdapter::list_archives() const {
    std::vector<ZimInfo> out;
    for (const auto& oa : archives_) {
        ZimInfo info;
        info.filename = oa.path;
        try { info.human_name = oa.arch->getMetadata("Name"); } catch (...) {}
        try { info.description = oa.arch->getMetadata("Description"); } catch (...) {}
        try { info.language = oa.arch->getMetadata("Language"); } catch (...) {}
        info.entry_count = oa.arch->getEntryCount();
        info.article_count = oa.arch->getArticleCount();
        out.push_back(info);
    }
    return out;
}

static std::string basename(const std::string& path) {
    auto slash = path.find_last_of('/');
    return (slash != std::string::npos) ? path.substr(slash+1) : path;
}

KiwixAdapter::ZimEntry KiwixAdapter::extract_entry(const std::string& zim_name,
                                                   const zim::Entry& e) const {
    ZimEntry out;
    out.zim_name = zim_name;
    out.title = e.getTitle();
    out.path = e.getPath();
    try { out.is_redirect = e.isRedirect(); } catch (...) {}

    if (!out.is_redirect) {
        try {
            auto item = e.getItem(true);  // follow redirects
            out.mimetype = item.getMimetype();
            out.size = item.getSize();
            const zim::size_type MAX = 256 * 1024;
            auto blob = item.getData(0, std::min(out.size, MAX));
            out.content.assign(blob.data(), blob.size());
        } catch (const std::exception& ex) {
            out.content = "(error: " + std::string(ex.what()) + ")";
        }
    } else {
        try {
            auto target = e.getRedirectEntry();
            out.path = target.getPath();
            out.title = target.getTitle();
        } catch (...) {}
    }
    return out;
}

std::vector<KiwixAdapter::ZimEntry> KiwixAdapter::get_by_title(const std::string& title) const {
    std::vector<ZimEntry> out;
    for (const auto& oa : archives_) {
        try {
            auto e = oa.arch->getEntryByTitle(title);
            out.push_back(extract_entry(basename(oa.path), e));
        } catch (...) {}
    }
    return out;
}

std::vector<KiwixAdapter::ZimEntry> KiwixAdapter::get_by_path(const std::string& path) const {
    std::vector<ZimEntry> out;
    for (const auto& oa : archives_) {
        try {
            auto e = oa.arch->getEntryByPath(path);
            out.push_back(extract_entry(basename(oa.path), e));
        } catch (...) {}
    }
    return out;
}

} // namespace muji
