// src/native/wn_cli.cpp
// === NATIVE ENTRY POINT for WordNet (layer 1) ===
//
// Standalone CLI: wraps WordNet 3.0 C API directly.
// Bypasses MUJI glue layer entirely — prove the adapter pattern works.
//
// Compile independently:
//   g++ -std=c++17 src/native/wn_cli.cpp -lwordnet -o wn_cli
//   (requires: apt install wordnet-dev wordnet)

#include <wn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static void usage(const char* p) {
    std::fprintf(stderr,
        "Usage: %s <word> [options]\n"
        "  -n   POS: noun only\n"
        "  -v   POS: verb only\n"
        "  -a   POS: adj only\n"
        "  -r   POS: adv only\n"
        "  -s   show synonyms (all senses)\n"
        "  -h   show hypernyms (is-a tree, depth 3)\n"
        "  -y   show hyponyms\n"
        "  -A   show antonyms\n"
        "  -d   show definitions\n"
        "  -p <path>  WordNet dict path [default: /usr/share/wordnet]\n"
        "  -H   help\n",
        p);
}

static void print_synsets(const char* word, int pos_filter, bool defs) {
    for (int p = NOUN; p <= ADV; ++p) {
        if (pos_filter && pos_filter != p) continue;
        char buf[256]; std::snprintf(buf, sizeof(buf), "%s", word);
        SynsetPtr first = findtheinfo_ds(buf, p, SYNS, ALLSENSES);
        if (!first) continue;
        const char* pname[] = {"", "n", "v", "a", "r"};
        int sense = 0;
        for (SynsetPtr s = first; s; s = s->nextss) {
            ++sense;
            std::printf("[%s] sense %d  offset=%ld\n", pname[p], sense, s->hereiam);
            if (s->words && s->wcount > 0) {
                std::printf("  lemmas: ");
                for (int i = 0; i < s->wcount; ++i) {
                    std::printf("%s%s", s->words[i], (i+1 < s->wcount) ? ", " : "");
                }
                std::printf("\n");
            }
            if (defs && s->defn) std::printf("  defn:   %s\n", s->defn);
        }
        free_syns(first);
    }
}

static void print_tree(SynsetPtr s, int ptr_type, int depth, int max_depth, const char* indent) {
    if (!s || depth > max_depth) return;
    char buf[512] = {0};
    if (s->words && s->words[0]) std::snprintf(buf, sizeof(buf), "%s", s->words[0]);
    std::printf("%s%s (%ld)\n", indent, buf, s->hereiam);

    SynsetPtr children = traceptrs_ds(s, ptr_type, 1, 1);
    for (SynsetPtr c = children; c; c = c->ptrlist) {
        char sub[512]; std::snprintf(sub, sizeof(sub), "  %s", indent);
        print_tree(c, ptr_type, depth + 1, max_depth, sub);
    }
    if (children) free_syns(children);
}

int main(int argc, char** argv) {
    if (argc < 2) { usage(argv[0]); return 1; }

    const char* word = nullptr;
    int pos_filter = 0;  // 0 = all
    bool do_syn = true, do_hyper = false, do_hypo = false, do_anto = false;
    bool do_defs = false;
    const char* dict_path = "/usr/share/wordnet";

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (!strcmp(a, "-H") || !strcmp(a, "--help")) { usage(argv[0]); return 0; }
        else if (!strcmp(a, "-n")) pos_filter = NOUN;
        else if (!strcmp(a, "-v")) pos_filter = VERB;
        else if (!strcmp(a, "-a")) pos_filter = ADJ;
        else if (!strcmp(a, "-r")) pos_filter = ADV;
        else if (!strcmp(a, "-s")) do_syn = true;
        else if (!strcmp(a, "-h")) { do_hyper = true; do_syn = false; }
        else if (!strcmp(a, "-y")) { do_hypo  = true; do_syn = false; }
        else if (!strcmp(a, "-A")) { do_anto  = true; do_syn = false; }
        else if (!strcmp(a, "-d")) do_defs = true;
        else if (!strcmp(a, "-p") && i+1 < argc) dict_path = argv[++i];
        else if (a[0] != '-' && !word) word = a;
    }

    if (!word) { usage(argv[0]); return 1; }

    setenv("WNSEARCHPATH", dict_path, 1);
    if (wninit() != 0) {
        std::fprintf(stderr, "wninit() failed — is WordNet installed? dict at: %s\n", dict_path);
        return 2;
    }

    if (do_syn)   print_synsets(word, pos_filter, do_defs);

    if (do_hyper) {
        char buf[256]; std::snprintf(buf, sizeof(buf), "%s", word);
        SynsetPtr first = findtheinfo_ds(buf, pos_filter ? pos_filter : NOUN, HYPERPTR, ALLSENSES);
        if (first) {
            std::printf("=== Hypernym tree (is-a) for '%s' ===\n", word);
            for (SynsetPtr s = first; s; s = s->nextss) {
                print_tree(s, HYPERPTR, 0, 3, "");
                std::printf("---\n");
            }
            free_syns(first);
        }
    }

    if (do_hypo) {
        char buf[256]; std::snprintf(buf, sizeof(buf), "%s", word);
        SynsetPtr first = findtheinfo_ds(buf, pos_filter ? pos_filter : NOUN, HYPOPTR, ALLSENSES);
        if (first) {
            std::printf("=== Hyponym tree (has-a) for '%s' ===\n", word);
            for (SynsetPtr s = first; s; s = s->nextss) {
                print_tree(s, HYPOPTR, 0, 2, "");
                std::printf("---\n");
            }
            free_syns(first);
        }
    }

    return 0;
}
