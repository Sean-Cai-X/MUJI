// src/native/mecab_cli.cpp
// === NATIVE ENTRY POINT for MeCab (layer 0b) ===
//
// Standalone command-line tool that uses MeCab C API DIRECTLY,
// bypassing the MUJI glue layer entirely. It exists to guarantee the
// design rule: "every native module keeps its own entry points, upgradeable
// independently of the glue layer."
//
// Compile independently (no MUJI deps, no ImGui):
//   g++ -O2 -std=c++17 -I<mecab_include> src/native/mecab_cli.cpp \
//       -L<mecab_lib> -lmecab -o bin/mecab_cli
//
// Or build via MUJI CMake: target 'mecab_cli'
//
// Output format (one token per line, tab-separated):
//   surface<TAB>pos<TAB>lemma<TAB>reading<TAB>start<TAB>end
//
// Exit codes:
//   0 = success
//   1 = usage error
//   2 = init failure (dicdir wrong?)

#include <mecab.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <iostream>

static void print_usage(const char* prog) {
    std::fprintf(stderr,
        "Usage: %s [options] <text>\n"
        "  -d <dir>     dicdir (required if no mecabrc)\n"
        "  -r <file>    rcfile path (default: /etc/mecabrc)\n"
        "  -u <file>    user dictionary (optional)\n"
        "  -b           batch mode: read texts from stdin, one per line\n"
        "  -T           also print per-token byte offsets (start<TAB>end)\n"
        "  -O <fmt>     output format: wakati | none | chasen\n"
        "  -a           output all morphs (lattice level 2)\n"
        "  -N <n>       N-best results (default 1)\n"
        "  -h           show this help\n"
        "\n"
        "Examples:\n"
        "  %s -d /var/lib/mecab/dic/ipadic-utf8 \"情報処理推進機構\"\n"
        "  echo '東京大学' | %s -b -d /var/lib/mecab/dic/ipadic-utf8\n",
        prog, prog, prog
    );
}

int main(int argc, char** argv) {
    const char* dicdir = nullptr;
    const char* rcfile = nullptr;
    const char* userdic = nullptr;
    const char* text = nullptr;
    bool batch = false;
    bool show_offsets = false;
    int nbest = 1;

    // 收集 mecab_new 的 argv
    std::vector<const char*> mecab_argv;
    mecab_argv.push_back("mecab_cli");  // argv[0]

    auto add_arg = [&](const char* a) { mecab_argv.push_back(a); };

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) { print_usage(argv[0]); return 0; }
        else if (!strcmp(a, "-d") && i+1 < argc) {
            dicdir = argv[++i];
            add_arg("-d"); add_arg(dicdir);
        }
        else if (!strcmp(a, "-r") && i+1 < argc) {
            rcfile = argv[++i];
            add_arg("-r"); add_arg(rcfile);
        }
        else if (!strcmp(a, "-u") && i+1 < argc) {
            userdic = argv[++i];
            add_arg("-u"); add_arg(userdic);
        }
        else if (!strcmp(a, "-b")) batch = true;
        else if (!strcmp(a, "-T")) show_offsets = true;
        else if (!strcmp(a, "-N") && i+1 < argc) {
            nbest = std::atoi(argv[++i]);
            char nb[16]; std::snprintf(nb, sizeof(nb), "-N%d", nbest);
            add_arg(nb);
        }
        else if (!strcmp(a, "-O") && i+1 < argc) {
            add_arg("-O"); add_arg(argv[++i]);
        }
        else if (!strcmp(a, "-a")) add_arg("-a");
        else if (a[0] != '-' && !text) text = a;
        else { std::fprintf(stderr, "Unknown arg: %s\n", a); print_usage(argv[0]); return 1; }
    }

    // 创建 MeCab 实例
    mecab_t* mecab = mecab_new(static_cast<int>(mecab_argv.size()),
                               const_cast<char**>(mecab_argv.data()));
    if (!mecab) {
        std::fprintf(stderr, "mecab_new failed (dicdir=%s): %s\n",
                     dicdir ? dicdir : "(default)",
                     mecab_strerror(nullptr));
        return 2;
    }

    auto process = [&](const std::string& s) {
        if (s.empty()) return;

        // mecab_sparse_tonode2: 简单高层 API, 直接返回 BOS 节点
        const mecab_node_t* node = mecab_sparse_tonode2(mecab, s.c_str(), s.size());
        if (!node) {
            std::fprintf(stderr, "parse failed: %s\n",
                         mecab_strerror(mecab));
            return;
        }

        int32_t cursor = 0;
        while (node) {
            if (node->stat == MECAB_BOS_NODE || node->stat == MECAB_EOS_NODE) {
                node = node->next;
                continue;
            }

            std::string surface(node->surface, node->length);
            std::string pos, lemma, reading;

            if (node->feature) {
                // "名詞,一般,*,*,*,*,東京,トウキョウ,トウキョウ"
                const size_t flen = strlen(node->feature);
                std::string fcopy(node->feature, flen);
                char* fc = &fcopy[0];
                char* fields[16] = {0};
                int nf = 0;
                char* p = fc;
                fields[nf++] = p;
                while ((p = strchr(p, ',')) && nf < 16) {
                    *p++ = '\0';
                    fields[nf++] = p;
                }
                if (nf >= 1) pos     = fields[0];
                if (nf >= 7) lemma   = fields[6];
                if (nf >= 8) reading = fields[7];
            }

            if (show_offsets) {
                std::printf("%s\t%s\t%s\t%s\t%d\t%d\n",
                            surface.c_str(), pos.c_str(),
                            lemma.c_str(), reading.c_str(),
                            cursor, cursor + (int32_t)node->length);
            } else {
                std::printf("%s\t%s\t%s\t%s\n",
                            surface.c_str(), pos.c_str(),
                            lemma.c_str(), reading.c_str());
            }

            cursor += (int32_t)node->length;
            node = node->next;
        }
        std::printf("EOS\n\n");
    };

    if (batch) {
        std::string line;
        while (std::getline(std::cin, line)) process(line);
    } else if (text) {
        process(std::string(text));
    } else {
        std::fprintf(stderr, "Error: no text given (use -b for batch or pass text argument)\n");
        mecab_destroy(mecab);
        return 1;
    }

    mecab_destroy(mecab);
    return 0;
}
