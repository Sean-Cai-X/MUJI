// kiwix_cli — native CLI for ZIM file reading
#include <iostream>
#include <string>
#include <vector>
#include <dirent.h>
#include <zim/archive.h>
#include <zim/entry.h>
#include <zim/item.h>
#include <zim/error.h>

static void usage() {
    std::cout
      << "kiwix_cli — ZIM reader CLI\n"
      << "Usage:\n"
      << "  kiwix_cli list -D <dir>           List ZIM files\n"
      << "  kiwix_cli list -f <file>          Single ZIM info\n"
      << "  kiwix_cli title <name> -D <dir>   Look up by title\n"
      << "  kiwix_cli path <path> -D <dir>    Look up by path\n";
}

static std::vector<std::string> scan_zim_dir(const std::string& zdir) {
    std::vector<std::string> files;
    DIR* d = opendir(zdir.c_str());
    if (!d) return files;
    struct dirent* ent;
    while ((ent = readdir(d))) {
        std::string n = ent->d_name;
        if (n.size() >= 4 && n.substr(n.size()-4) == ".zim")
            files.push_back(zdir + "/" + n);
    }
    closedir(d);
    return files;
}

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 1; }

    std::string cmd = argv[1];
    std::string zdir, zfile, title, path;

    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-D" && i+1 < argc) zdir = argv[++i];
        else if (a == "-f" && i+1 < argc) zfile = argv[++i];
        else if (a[0] != '-') {
            if (title.empty()) title = a;
            else if (path.empty()) path = a;
        }
    }

    if (cmd == "list") {
        std::vector<std::string> files;
        if (!zfile.empty()) files.push_back(zfile);
        else if (!zdir.empty()) files = scan_zim_dir(zdir);
        else { usage(); return 1; }

        for (auto& f : files) {
            std::string bn = f.substr(f.rfind('/')+1);
            try {
                zim::Archive arch(f);
                std::cout << "ZIM: " << bn << "\n";
                try { std::cout << "  Name:        " << arch.getMetadata("Name") << "\n"; } catch (...) {}
                try { std::cout << "  Description: " << arch.getMetadata("Description") << "\n"; } catch (...) {}
                try { std::cout << "  Language:    " << arch.getMetadata("Language") << "\n"; } catch (...) {}
                std::cout << "  Entries:     " << arch.getEntryCount() << "\n";
                std::cout << "  Articles:    " << arch.getArticleCount() << "\n";
            } catch (const std::exception& e) {
                std::cerr << "  ERROR: " << e.what() << "\n";
            }
        }
        return 0;
    }

    if ((cmd == "title" || cmd == "path") && !zdir.empty()) {
        auto files = scan_zim_dir(zdir);
        bool found = false;
        for (auto& f : files) {
            std::string bn = f.substr(f.rfind('/')+1);
            try {
                zim::Archive arch(f);
                zim::Entry e = (cmd == "title")
                    ? arch.getEntryByTitle(title)
                    : arch.getEntryByPath(path);

                std::cout << "ZIM: " << bn << "\n";
                std::cout << "  Title:     " << e.getTitle() << "\n";
                std::cout << "  Path:      " << e.getPath() << "\n";
                std::cout << "  Redirect:  " << (e.isRedirect() ? "yes" : "no") << "\n";
                if (!e.isRedirect()) {
                    auto item = e.getItem(true);
                    std::cout << "  Mimetype:  " << item.getMimetype() << "\n";
                    auto sz = item.getSize();
                    std::cout << "  Size:      " << sz << " bytes\n";
                    auto blob = item.getData(0, std::min(sz, (zim::size_type)1024));
                    std::string preview(blob.data(), blob.size());
                    std::cout << "  Preview:\n";
                    bool in_tag = false;
                    for (char c : preview) {
                        if (c == '<') in_tag = true;
                        else if (c == '>') { in_tag = false; std::cout << ' '; }
                        else if (!in_tag) std::cout << c;
                    }
                    std::cout << "\n\n";
                }
                found = true;
            } catch (...) {}
        }
        if (!found) {
            std::cerr << "(not found)\n";
            return 4;
        }
        return 0;
    }

    usage();
    return 1;
}
