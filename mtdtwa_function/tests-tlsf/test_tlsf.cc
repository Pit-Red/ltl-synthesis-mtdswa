#include "extract_tlsf_info.hh"
#include "../solve_el_aut.hh"
#include <spot/tl/parse.hh>
#include <spot/twaalgos/dot.hh>
#include <spot/twaalgos/translate.hh>
#include <spot/twaalgos/zlktree.hh>
#include <spot/twaalgos/mtdtwa.hh>
#include <spot/twaalgos/dot.hh>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <regex>
#include <string>

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

void save_datastructures(spot::twa_graph_ptr aut,  std::vector<std::string> names_datastructures = {"aut", "mtdta", "zl_tree"}) {

    spot::mtdtwa_ptr mtdta = spot::dtwa_to_mtdtwa(aut);
    spot::zielonka_tree zl_tree = spot::zielonka_tree(aut->acc());

    std::string dot_folder = "img/";

    for (const auto& name : names_datastructures) {
        std::string dot_path = dot_folder + name + ".dot";
        std::ofstream dot_out(dot_path);
        if (!dot_out) {
            std::cerr << "Error: cannot write to " << dot_path << "\n";
            return;
        }

        if (name == "aut")
            spot::print_dot(dot_out, aut);
        else if (name == "mtdta")
            mtdta->print_dot(dot_out);
        else if (name == "zl_tree")
            zl_tree.dot(dot_out);

        dot_out.close();
         
        // printing the PNG
        std::string png_path = dot_folder + name + ".png";
        std::string cmd = "dot -Tpng " + dot_path + " -o " + png_path;
        int ret = std::system(cmd.c_str());
        if (ret != 0)
            std::cerr << "Warning: GraphViz 'dot' failed (code " << ret << ")\n";
        else
            std::cout << "PNG saved to " << png_path << "\n";

        std::remove(dot_path.c_str());
    }

}

int main(int argc, char const *argv[])
{
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <path_to_tlsf_file>\n";
        return 1;
    }

    spot::bdd_dict_preorder dict;
    std::unordered_set<spot::formula> come_first;

    const std::string path = argv[1];
    TlsfInfo info = extract_tlsf_info(path);

    std::cout << "Extracted information:\n";
    std::cout << "INPUTS:\n";
    for (const auto& s : info.inputs) {
        std::cout << "  - " << s << '\n';
    }
    std::cout << "\nOUTPUTS:\n";
    for (const auto& s : info.outputs) {
        std::cout << "  - " << s << '\n';   
    }
    std::cout << "\nLTL:\n" << info.ltl << '\n';
    std::cout << "\nSemantics: " << (info.mealy_semantics ? "Mealy" : "Moore") << "\n\n";

    // Building the autmaton from the extracted information
    spot::formula f = spot::parse_formula(info.ltl);

    if (info.mealy_semantics)
        for (const std::string& s: info.inputs)
            come_first.insert(spot::formula::ap(s));
    else
        for (const std::string& s: info.outputs)
            come_first.insert(spot::formula::ap(s));

    f.traverse([&](const spot::formula& f) {
        if (f.is(spot::op::ap) && (come_first.find(f) != come_first.end()))
        dict.register_proposition(f);
        return false;
    });

    spot::translator trans(dict);
    trans.set_pref(spot::postprocessor::Deterministic | spot::postprocessor::Complete);
    spot::twa_graph_ptr aut = trans.run(f);

    std::cout << "is deterministic? " << aut->prop_universal() << "\n";
    std::cout << "num states: " << aut->num_states() << "\n";

    save_datastructures(aut, {"zl_tree"});

    StateSet w_partition = SolveAutEL(aut, info.outputs);

    for (const auto& state : w_partition) {
        std::cout << "Winning state: " << state << "\n";
    }

    return 0;
}
