// src/native/jieba_cli.cpp
// === NATIVE ENTRY POINT for cppjieba (layer 0) ===
//
// This is a standalone command-line tool that uses cppjieba DIRECTLY,
// bypassing the MUJI glue layer entirely. It exists to guarantee the
// design rule: "every native module keeps its own entry points, upgradeable
// independently of the glue layer."
//
// Compile independently (no MUJI deps, no ImGui):
//   g++ -O2 -std=c++17 -I3rdparty/cppjieba/include \
//       src/native/jieba_cli.cpp -o bin/jieba_cli
//
// Or build via MUJI CMake: target 'jieba_cli'
//
// Output format (one token per line, tab-separated):
//   word<TAB>pos<TAB>start<TAB>end
// Empty lines separate input texts when --batch is used.

#include <cppjieba/Jieba.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <iostream>

static void print_usage(const char* prog) {
    std::fprintf(stderr,
        "Usage: %s [options] <text>\n"
        "  -m <mode>     ex(act) | full | sea(rch)   [default: ex]\n"
        "  -d <dict>     path to jieba.dict.utf8\n"
        "  -h <hmm>      path to hmm_model.utf8\n"
        "  -u <user>     path to user dict (optional)\n"
        "  -i <idf>      path to idf.utf8\n"
        "  -s <stop>     path to stop_words.utf8\n"
        "  -b            batch mode: read texts from stdin, one per line\n"
        "  -T            also print per-token byte offsets\n"
        "  -h            show this help\n"
        "\n"
        "Examples:\n"
        "  %s -m ex \"NUMA架构下，跨节点内存访问延迟高于本地内存\"\n"
        "  echo '测试文本' | %s -b -m full\n",
        prog, prog, prog
    );
}

int main(int argc, char** argv) {
    const char* dict_path = "3rdparty/cppjieba/dict/jieba.dict.utf8";
    const char* hmm_path  = "3rdparty/cppjieba/dict/hmm_model.utf8";
    const char* user_path = nullptr;
    const char* idf_path  = "3rdparty/cppjieba/dict/idf.utf8";
    const char* stop_path = "3rdparty/cppjieba/dict/stop_words.utf8";
    enum Mode { Exact, Full, Search } mode = Exact;
    bool batch = false;
    bool show_offsets = false;
    const char* text = nullptr;

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) { print_usage(argv[0]); return 0; }
        else if (!strcmp(a, "-m") && i+1 < argc) {
            const char* m = argv[++i];
            if (!strncmp(m, "ex", 2))   mode = Exact;
            else if (!strncmp(m, "full", 4)) mode = Full;
            else if (!strncmp(m, "sea", 3))  mode = Search;
            else { std::fprintf(stderr, "Unknown mode: %s\n", m); return 1; }
        }
        else if (!strcmp(a, "-d") && i+1 < argc) dict_path = argv[++i];
        else if (!strcmp(a, "-h") && i+1 < argc) hmm_path  = argv[++i];
        else if (!strcmp(a, "-u") && i+1 < argc) user_path = argv[++i];
        else if (!strcmp(a, "-i") && i+1 < argc) idf_path  = argv[++i];
        else if (!strcmp(a, "-s") && i+1 < argc) stop_path = argv[++i];
        else if (!strcmp(a, "-b")) batch = true;
        else if (!strcmp(a, "-T")) show_offsets = true;
        else if (a[0] != '-' && !text) text = a;
        else { std::fprintf(stderr, "Unknown arg: %s\n", a); print_usage(argv[0]); return 1; }
    }

    try {
        cppjieba::Jieba jieba(
            dict_path,
            hmm_path,
            user_path ? user_path : "",
            idf_path,
            stop_path);

        auto process = [&](const std::string& s) {
            if (s.empty()) return;
            std::vector<std::pair<std::string, std::string>> tagged;
            std::vector<std::string> cut;

            if (mode == Exact) {
                jieba.Tag(s, tagged);
                int32_t cursor = 0;
                for (auto& p : tagged) {
                    size_t found = s.find(p.first, cursor);
                    if (found == std::string::npos) found = cursor;
                    if (show_offsets)
                        std::printf("%s\t%s\t%d\t%d\n",
                            p.first.c_str(), p.second.c_str(),
                            (int)found, (int)(found + p.first.size()));
                    else
                        std::printf("%s\t%s\n", p.first.c_str(), p.second.c_str());
                    cursor = found + p.first.size();
                }
            } else if (mode == Full) {
                jieba.Cut(s, cut, true);
                for (auto& w : cut) std::printf("%s\n", w.c_str());
            } else {
                jieba.CutForSearch(s, cut);
                for (auto& w : cut) std::printf("%s\n", w.c_str());
            }
            std::printf("\n");
        };

        if (batch) {
            std::string line;
            while (std::getline(std::cin, line)) process(line);
        } else if (text) {
            process(text);
        } else {
            std::fprintf(stderr, "Error: no text given (use -b for batch or pass text argument)\n");
            return 1;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "cppjieba init error: %s\n", e.what());
        return 2;
    }

    return 0;
}
