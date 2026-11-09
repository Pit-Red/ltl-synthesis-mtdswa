#include "extract_tlsf_info.hh"
#include "../solve_el_aut_swa.hh"
#include <spot/tl/parse.hh>
#include <spot/twaalgos/dot.hh>
#include <spot/twaalgos/translate.hh>
#include <spot/twaalgos/zlktree.hh>
#include <spot/twaalgos/dot.hh>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <regex>
#include <string>
#include <unordered_set>

// bool extract_status_from_tlsf_file(const std::string& path) {
//     std::ifstream in(path);
//     if (!in) return false;

//     std::string line;
//     std::regex status_re(R"(^\s*//\s*STATUS\s*:\s*(.+?)\s*$)", std::regex::icase);

//     while (std::getline(in, line)) {
//         std::smatch m;
//         if (std::regex_match(line, m, status_re)) {
//             return m[1].str().compare("realizable") == 0;
//         }
//     }

//     return false;
// }

// Writes img/zl_tree.png (Zielonka tree of the acceptance condition).
// Only on request (--png): it overwrites the same file on every run.
static void save_zl_tree_png(const spot::twa_graph_ptr& aut) {
    spot::zielonka_tree zl_tree(aut->acc());
    const std::string dot_path = "img/zl_tree.dot";
    const std::string png_path = "img/zl_tree.png";
    {
        std::ofstream dot_out(dot_path);
        if (!dot_out) {
            std::cerr << "Error: cannot write to " << dot_path << "\n";
            return;
        }
        zl_tree.dot(dot_out);
    }
    const std::string cmd = "dot -Tpng " + dot_path + " -o " + png_path;
    int ret = std::system(cmd.c_str());
    if (ret != 0)
        std::cerr << "Warning: GraphViz 'dot' failed (code " << ret << ")\n";
    else
        std::cout << "PNG saved to " << png_path << "\n";
    std::remove(dot_path.c_str());
}

// Exit codes: 0 = MTDSWA agrees with ltlsynt, 1 = MISMATCH,
//             2 = inconclusive (error, timeout, or ltlsynt not run).
int main(int argc, char const *argv[])
{
    std::string path;
    bool png = false;
    bool run_ltlsynt = true;
    unsigned ltlsynt_timeout = 60;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--png") png = true;
        else if (a == "--no-ltlsynt") run_ltlsynt = false;
        else if (a.rfind("--ltlsynt-timeout=", 0) == 0)
            ltlsynt_timeout = std::stoul(a.substr(18));
        else if (path.empty()) path = a;
        else { path.clear(); break; }
    }
    if (path.empty()) {
        std::cerr << "Usage: " << argv[0]
                  << " <path_to_tlsf_file> [--png] [--no-ltlsynt] [--ltlsynt-timeout=SECONDS]\n";
        return 2;
    }

    // Unbuffered, so a run killed by a time or memory limit still shows
    // how far it got (translation vs. solving).
    std::cout << std::unitbuf;

    try {
        TlsfInfo info = extract_tlsf_info(path);

        std::cout << "File: " << path << "\n";
        std::cout << "INPUTS: ";
        for (const auto& s : info.inputs) std::cout << s << ' ';
        std::cout << "\nOUTPUTS: ";
        for (const auto& s : info.outputs) std::cout << s << ' ';
        std::cout << "\nLTL: " << info.ltl << '\n';
        std::cout << "Semantics: " << info.semantics
                  << " (" << (info.mealy_semantics ? "Mealy" : "Moore") << ")\n";

        // Turn order is encoded only through the BDD variable order:
        // the player whose variables are registered first moves first.
        //   Mealy: inputs first (environment moves, then system answers)
        //   Moore: outputs first (system commits, then environment moves)
        spot::bdd_dict_preorder dict;
        std::unordered_set<spot::formula> come_first;
        for (const std::string& s : info.mealy_semantics ? info.inputs : info.outputs)
            come_first.insert(spot::formula::ap(s));

        spot::formula f = spot::parse_formula(info.ltl);
        f.traverse([&](const spot::formula& g) {
            if (g.is(spot::op::ap) && come_first.count(g))
                dict.register_proposition(g);
            return false;
        });

        spot::translator trans(dict);
        trans.set_type(spot::postprocessor::Generic);
        trans.set_pref(spot::postprocessor::Deterministic | spot::postprocessor::Complete);
        std::cout << "Translating...\n";
        spot::twa_graph_ptr aut = trans.run(f);

        std::cout << "Deterministic: " << aut->prop_universal() << "\n";
        std::cout << "States: " << aut->num_states() << "\n";
        std::cout << "Acceptance: " << aut->get_acceptance() << "\n";

        if (png)
            save_zl_tree_png(aut);

        // MTDTWA (SolveAutEL) is not used here: it has a known bug.
        // dtwa_to_mtdswa maps the initial state to root 0, so the
        // specification is realizable iff state 0 is winning.
        std::cout << "Solving with MTDSWA...\n";
        StateSet win = SolveAutEL_swa(aut, info.outputs);
        const bool swa_realizable = win.count(0) > 0;
        std::cout << "Realizable (mtdswa): " << (swa_realizable ? "Yes" : "No") << "\n";

        if (!run_ltlsynt)
            return 2;

        LtlsyntTlsfResult ref = ltlsynt_tlsf_realizability(path, info, ltlsynt_timeout);
        std::cout << "Mealy-converted LTL (syfco -os): " << ref.mealy_formula << "\n";
        std::cout << "Realizable (ltlsynt): " << to_string(ref.verdict) << "\n";
        if (ref.verdict == LtlsyntVerdict::Timeout || ref.verdict == LtlsyntVerdict::Error) {
            if (ref.verdict == LtlsyntVerdict::Error)
                std::cerr << "ltlsynt output:\n" << ref.raw_output;
            std::cout << "RESULT: INCONCLUSIVE\n";
            return 2;
        }
        const bool ref_realizable = ref.verdict == LtlsyntVerdict::Realizable;
        if (ref_realizable != swa_realizable) {
            std::cout << "RESULT: MISMATCH\n";
            return 1;
        }
        std::cout << "RESULT: MATCH\n";
        return 0;
    } catch (const std::exception& e) {
        std::cout << "ERROR: " << e.what() << "\n";
        std::cout << "RESULT: INCONCLUSIVE\n";
        return 2;
    }
}
